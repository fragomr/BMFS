/* BareMetal File System Utility */
/* Written by Ian Seyler of Return Infinity */
/* v1.2.3 (2017 04 07) */

/* Global includes */
#include "libbmfs.h"
#include <errno.h>
#include <limits.h>

/* Global constants */
// Min disk size is 6MiB (three blocks of 2MiB each.)
const unsigned int minimumDiskSize = (6 * 1024 * 1024);
// Block size is 2MiB
const unsigned int blockSize = 2 * 1024 * 1024;

/* Global variables */
FILE *file, *disk;
unsigned int filesize, disksize, retval;
char tempfilename[32], tempstring[32];
char *filename, *diskname, *command;
const char fs_tag[] = "BMFS";
struct BMFSEntry entry;
void *pentry = &entry;
char *BlockMap;
char *FileBlocks;
char Directory[4096];
char DiskInfo[512];


void bmfs_dir_zero(struct BMFSDir *dir)
{
	memset(dir, 0, sizeof(*dir));
}


int bmfs_opendir(struct BMFSDir *dir, const char *path)
{
	disk = fopen(path, "r+b");
	if (disk == NULL)
		return ENOENT;
	return bmfs_readdir(dir, file);
}


int bmfs_readdir(struct BMFSDir *dir, FILE *file)
{
	bmfs_dir_zero(dir);

	fseek(file, 4096, SEEK_SET);
	fread(dir->Entries, 1, sizeof(dir->Entries), file);
	fseek(file, 0, SEEK_SET);
	return 0;
}


int bmfs_savedir(const struct BMFSDir *dir)
{
	fseek(disk, 4096, SEEK_SET);
	fwrite(dir->Entries, 1, sizeof(dir->Entries), disk);
	return 0;
}


struct BMFSEntry * bmfs_find(struct BMFSDir *dir, const char *filename)
{
	int tint;
	for (tint = 0; tint < 64; tint++)
	{
		if (dir->Entries[tint].FileName[0] == 0)
			/* end of directory */
			break;
		else if (dir->Entries[tint].FileName[0] == 1)
			/* skip empty entry */
			continue;
		else if (strcmp(dir->Entries[tint].FileName, filename) != 0)
			/* not a match, skip this file */
			continue;

		/* file was found */
		return &dir->Entries[tint];
	}

	/* file not found */
	return NULL;
}


int bmfs_findfile(const char *filename, struct BMFSEntry *fileentry, int *entrynumber)
{
	struct BMFSDir dir;
	struct BMFSEntry *result;

	if (bmfs_readdir(&dir, disk) != 0)
		return 0;

	result = bmfs_find(&dir, filename);
	if (result == NULL)
		/* not found */
		return 0;

	if (fileentry)
		*fileentry = *result;

	if (entrynumber)
		*entrynumber = (result - &dir.Entries[0]) / sizeof(dir.Entries[0]);

	/* entry found */
	return 1;
}


void bmfs_list()
{
	int tint;

	printf("Disk Size: %d MiB\n", disksize);
	printf("Name                            |            Size (B)|      Reserved (MiB)\n");
	printf("==========================================================================\n");
	for (tint = 0; tint < 64; tint++)				// Max 64 entries
	{
		memcpy(pentry, Directory+(tint*64), 64);
		if (entry.FileName[0] == 0x00)				// End of directory, bail out
		{
			tint = 64;
		}
		else if (entry.FileName[0] == 0x01)			// Emtpy entry
		{
			// Ignore
		}
		else							// Valid entry
		{
			printf("%-32s %20lld %20lld\n", entry.FileName, (long long int)entry.FileSize, (long long int)(entry.ReservedBlocks*2));
		}
	}
}


void bmfs_format()
{
	memset(DiskInfo, 0, 512);
	memset(Directory, 0, 4096);
	memcpy(DiskInfo, fs_tag, 4);                    // Add the 'BMFS' tag
	fseek(disk, 1024, SEEK_SET);                    // Seek 1KiB in for disk information
	fwrite(DiskInfo, 512, 1, disk);                 // Write 512 bytes for the DiskInfo
	fseek(disk, 4096, SEEK_SET);                    // Seek 4KiB in for directory
	fwrite(Directory, 4096, 1, disk);               // Write 4096 bytes for the Directory
}


int bmfs_initialize(char *diskname, char *size, char *mbr, char *boot, char *kernel)
{
	unsigned long long diskSize = 0;
	unsigned long long writeSize = 0;
	const char *bootFileType = NULL;
	size_t bufferSize = 50 * 1024;
	char * buffer = NULL;
	FILE *mbrFile = NULL;
	FILE *bootFile = NULL;
	FILE *kernelFile = NULL;
	int diskSizeFactor = 0;
	size_t chunkSize = 0;
	int ret = 0;
	size_t i;

	// Determine how the second file will be described in output messages.
	// If a kernel file is specified too, then assume the second file is the
	// boot loader.  If no kernel file is specified, assume the boot loader
	// and kernel are combined into one system file.
	if (boot != NULL)
	{
		bootFileType = "boot loader";
		if (kernel == NULL)
		{
			bootFileType = "system";
		}
	}

	// Validate the disk size string and convert it to an integer value.
	for (i = 0; size[i] != '\0' && ret == 0; ++i)
	{
		char ch = size[i];
		if (isdigit(ch))
		{
			unsigned int n = ch - '0';
			if (diskSize * 10 > diskSize ) // Make sure we don't overflow
			{
				diskSize *= 10;
				diskSize += n;
			}
			else if (diskSize == 0) // First loop iteration
			{
				diskSize += n;
			}
			else
			{
				printf("Error: Disk size is too large\n");
				ret = 1;
			}
		}
		else if (i == 0) // No digits specified
		{
			printf("Error: A numeric disk size must be specified\n");
			ret = 1;
		}
		else
		{
			switch (toupper(ch))
			{
					case 'K':
						diskSizeFactor = 1;
						break;
					case 'M':
						diskSizeFactor = 2;
						break;
					case 'G':
						diskSizeFactor = 3;
						break;
					case 'T':
						diskSizeFactor = 4;
						break;
					case 'P':
						diskSizeFactor = 5;
						break;
					default:
						printf("Error: Invalid disk size string: '%s'\n", size);
						ret = 1;
						break;
			}

			// If this character is a valid unit indicator, but is not at the
			// end of the string, then the string is invalid.
			if (ret == 0 && size[i+1] != '\0')
			{
				printf("Error: Invalid disk size string: '%s'\n", size);
				ret = 1;
			}
		}
	}

	// Adjust the disk size if a unit indicator was given.  Note that an
	// input of something like "0" or "0K" will get past the checks above.
	if (ret == 0 && diskSize > 0 && diskSizeFactor > 0)
	{
		while (diskSizeFactor--)
		{
			if (diskSize * 1024 > diskSize ) // Make sure we don't overflow
			{
				diskSize *= 1024;
			}
			else
			{
				printf("Error: Disk size is too large\n");
				ret = 1;
			}
		}
	}

	// Make sure the disk size is large enough.
	if (ret == 0)
	{
		if (diskSize < minimumDiskSize)
		{
			printf( "Error: Disk size must be at least %d bytes (%dMiB)\n", minimumDiskSize, minimumDiskSize / (1024*1024));
			ret = 1;
		}
	}

	// Open the Master boot Record file for reading.
	if (ret == 0 && mbr != NULL)
	{
		mbrFile = fopen(mbr, "rb");
		if (mbrFile == NULL )
		{
			printf("Error: Unable to open MBR file '%s'\n", mbr);
			ret = 1;
		}
	}

	// Open the boot loader file for reading.
	if (ret == 0 && boot != NULL)
	{
		bootFile = fopen(boot, "rb");
		if (bootFile == NULL )
		{
			printf("Error: Unable to open %s file '%s'\n", bootFileType, boot);
			ret = 1;
		}
	}

	// Open the kernel file for reading.
	if (ret == 0 && kernel != NULL)
	{
		kernelFile = fopen(kernel, "rb");
		if (kernelFile == NULL )
		{
			printf("Error: Unable to open kernel file '%s'\n", kernel);
			ret = 1;
		}
	}

	// Allocate buffer to use for filling the disk image with zeros.
	if (ret == 0)
	{
		buffer = (char *) malloc(bufferSize);
		if (buffer == NULL)
		{
			printf("Error: Failed to allocate buffer\n");
			ret = 1;
		}
	}

	// Open the disk image file for writing.  This will truncate the disk file
	// if it already exists, so we should do this only after we're ready to
	// actually write to the file.
	if (ret == 0)
	{
		disk = fopen(diskname, "wb");
		if (disk == NULL)
		{
			printf("Error: Unable to open disk '%s'\n", diskname);
			ret = 1;
		}
	}

	// Fill the disk image with zeros.
	if (ret == 0)
	{
		double percent;
		memset(buffer, 0, bufferSize);
		writeSize = 0;
		while (writeSize < diskSize)
		{
			percent = writeSize;
			percent /= diskSize;
			percent *= 100;
			printf("Formatting disk: %llu of %llu bytes (%.0f%%)...\r", writeSize, diskSize, percent);
			chunkSize = bufferSize;
			if (chunkSize > diskSize - writeSize)
			{
				chunkSize = diskSize - writeSize;
			}
			if (fwrite(buffer, chunkSize, 1, disk) != 1)
			{
				printf("Error: Failed to write disk '%s'\n", diskname);
				ret = 1;
				break;
			}
			writeSize += chunkSize;
		}
		if (ret == 0)
		{
			printf("Formatting disk: %llu of %llu bytes (100%%)%9s\n", writeSize, diskSize, "");
		}
	}

	// Format the disk.
	if (ret == 0)
	{
		rewind(disk);
		bmfs_format();
	}

	// Write the master boot record if it was specified by the caller.
	if (ret == 0 && mbrFile != NULL)
	{
		fseek(disk, 0, SEEK_SET);
		if (fread(buffer, 512, 1, mbrFile) == 1)
		{
			if (fwrite(buffer, 512, 1, disk) != 1)
			{
				printf("Error: Failed to write disk '%s'\n", diskname);
				ret = 1;
			}
		}
		else
		{
			printf("Error: Failed to read file '%s'\n", mbr);
			ret = 1;
		}
	}

	// Write the boot loader if it was specified by the caller.
	if (ret == 0 && bootFile != NULL)
	{
		fseek(disk, 8192, SEEK_SET);
		for (;;)
		{
			chunkSize = fread( buffer, 1, bufferSize, bootFile);
			if (chunkSize > 0)
			{
				if (fwrite(buffer, chunkSize, 1, disk) != 1)
				{
					printf("Error: Failed to write disk '%s'\n", diskname);
					ret = 1;
				}
			}
			else
			{
				if (ferror(disk))
				{
					printf("Error: Failed to read file '%s'\n", boot);
					ret = 1;
				}
				break;
			}
		}
	}

	// Write the kernel if it was specified by the caller. The kernel must
	// immediately follow the boot loader on disk (i.e. no seek needed.)
	if (ret == 0 && kernelFile != NULL)
	{
		for (;;)
		{
			chunkSize = fread( buffer, 1, bufferSize, kernelFile);
			if (chunkSize > 0)
			{
				if (fwrite(buffer, chunkSize, 1, disk) != 1)
				{
					printf("Error: Failed to write disk '%s'\n", diskname);
					ret = 1;
				}
			}
			else
			{
				if (ferror(disk))
				{
					printf("Error: Failed to read file '%s'\n", kernel);
					ret = 1;
				}
				break;
			}
		}
	}

	// Close any files that were opened.
	if (mbrFile != NULL)
	{
		fclose(mbrFile);
	}
	if (bootFile != NULL)
	{
		fclose(bootFile);
	}
	if (kernelFile != NULL)
	{
		fclose(kernelFile);
	}
	if (disk != NULL)
	{
		fclose(disk);
		disk = NULL;
	}

	// Free the buffer if it was allocated.
	if (buffer != NULL)
	{
		free(buffer);
	}

	if (ret == 0)
	{
		printf("Disk initialization complete.\n");
	}

	return ret;
}


// helper function for qsort, sorts by StartingBlock field
static int StartingBlockCmp(const void *pa, const void *pb)
{
	struct BMFSEntry *ea = (struct BMFSEntry *)pa;
	struct BMFSEntry *eb = (struct BMFSEntry *)pb;
	// empty records go to the end
	if (ea->FileName[0] == 0x01)
		return 1;
	if (eb->FileName[0] == 0x01)
		return -1;
	// compare non-empty records by their starting blocks number
	return (ea->StartingBlock - eb->StartingBlock);
}

int bmfs_create(const char *filename, unsigned long long maxsize)
{
	struct BMFSEntry tempentry;
	int slot;

	if (maxsize % 2 != 0)
		maxsize++;

	if (bmfs_findfile(filename, &tempentry, &slot))
		return -EEXIST;

	unsigned long long blocks_requested = maxsize / 2; // how many blocks to allocate
	unsigned long long num_blocks = disksize / 2; // number of blocks in the disk
	int num_used_entries = 0; // how many entries of Directory are either used or deleted
	int first_free_entry = -1; // where to put new entry
	int tint;
	struct BMFSEntry *pEntry;
	unsigned long long new_file_start = 0;
	unsigned long long prev_file_end = 1;

	struct BMFSDir dir;
	if (bmfs_readdir(&dir, disk) != 0)
		return -ENOENT;

	// Calculate number of files
	for (tint = 0; tint < 64; tint++)
	{
		pEntry = &dir.Entries[tint]; // points to the current directory entry
		if (pEntry->FileName[0] == 0x00) // end of directory
		{
			num_used_entries = tint;
			if (first_free_entry == -1)
				first_free_entry = tint; // there were no unused entires before, will use this one
			break;
		}
		else if (pEntry->FileName[0] == 0x01) // unused entry
		{
			if (first_free_entry == -1)
				first_free_entry = tint; // will use it for our new file
		}
	}

	if (first_free_entry == -1)
	{
		printf("Error: Cannot create file. No free directory entries.\n");
		return -ENOSPC;
	}

	// Find an area with enough free blocks
	// Sort our copy of the directory by starting block number
	qsort(dir.Entries, num_used_entries, 64, StartingBlockCmp);

	for (tint = 0; tint < num_used_entries + 1; tint++)
	{
		// on each iteration of this loop we'll see if a new file can fit
		// between the end of the previous file (initially == 1)
		// and the beginning of the current file (or the last data block if there are no more files).

		unsigned long long this_file_start;
		pEntry = &dir.Entries[tint]; // points to the current directory entry

		if (tint == num_used_entries || pEntry->FileName[0] == 0x01)
			this_file_start = num_blocks - 1; // index of the last block
		else
			this_file_start = pEntry->StartingBlock;

		if (this_file_start - prev_file_end >= blocks_requested)
		{ // fits here
			new_file_start = prev_file_end;
			break;
		}

		if (tint < num_used_entries)
			prev_file_end = pEntry->StartingBlock + pEntry->ReservedBlocks;
	}

	if (new_file_start == 0)
	{
		printf("Error: Cannot create file of size %lld MiB.\n", maxsize);
		return -ENOSPC;
	}

	// Add file record to Directory
	pEntry = &dir.Entries[first_free_entry];
	pEntry->StartingBlock = new_file_start;
	pEntry->ReservedBlocks = blocks_requested;
	pEntry->FileSize = 0;
	strcpy(pEntry->FileName, filename);

	if (first_free_entry == num_used_entries && num_used_entries + 1 < 64)
	{
		// here we used the record that was marked with 0x00,
		// so make sure to mark the next record with 0x00 if it exists
		dir.Entries[num_used_entries + 1].FileName[0] = 0x00;
	}

	return bmfs_savedir(&dir);
}

// Read a file from a BMFS volume
void bmfs_readfile(char *filename)
{
	struct BMFSEntry tempentry;
	FILE *tfile;
	int slot, retval;
	unsigned long long bytestoread;
	char *buffer;

	if (0 == bmfs_findfile(filename, &tempentry, &slot))
	{
		printf("Error: File not found in BMFS.\n");
	}
	else
	{
		if ((tfile = fopen(tempentry.FileName, "wb")) == NULL)
		{
			printf("Error: Could not open local file '%s'\n", tempentry.FileName);
		}
		else
		{
			bytestoread = tempentry.FileSize;
			fseek(disk, tempentry.StartingBlock*blockSize, SEEK_SET); // Skip to the starting block in the disk
			buffer = malloc(blockSize);
			if (buffer == NULL)
			{
				printf("Error: Unable to allocate enough memory for buffer.\n");
			}
			else
			{
				while (bytestoread != 0)
				{
					if (bytestoread >= blockSize)
					{
						retval = fread(buffer, blockSize, 1, disk);
						if (retval == 1)
						{
							fwrite(buffer, blockSize, 1, tfile);
							bytestoread -= blockSize;
						}
						else
						{
							printf("Error: Unexpected read length detected.\n");
							bytestoread = 0;
						}
					}
					else
					{
						retval = fread(buffer, bytestoread, 1, disk);
						if (retval == 1)
						{
							fwrite(buffer, bytestoread, 1, tfile);
							bytestoread = 0;
						}
						else
						{
							printf("Error: Unexpected read length detected.\n");
							bytestoread = 0;
						}
					}
				}
			}
			fclose(tfile);
		}
	}
}

unsigned long long bmfs_read(const char *filename,
                             void *buf,
                             unsigned long long len,
                             unsigned long long off)
{
	struct BMFSEntry tempentry;

	if (bmfs_findfile(filename, &tempentry, NULL) == 0)
	{
		memcpy(buf, "h", 1);
		return 1;
	}

	/* Make sure the offset doesn't overflow */
	if (off > tempentry.FileSize)
		off = tempentry.FileSize;

	/* Make sure the read length doesn't overflow */
	if ((off + len) > tempentry.FileSize)
		len = tempentry.FileSize - off;

	/* Skip to the starting block in the disk */
	fseek(disk, (tempentry.StartingBlock*blockSize) + off, SEEK_SET);

	return fread(buf, 1, len, disk);
}

int bmfs_write(const char *filename,
               const void *buf,
               size_t len,
               off_t off)
{
	struct BMFSDir dir;
	if (bmfs_readdir(&dir, disk) != 0)
		return -ENOENT;

	struct BMFSEntry *entry;
	entry = bmfs_find(&dir, filename);
	if (entry == NULL)
		return -ENOENT;

	/* make sure fuse isn't
	 * screwing anything up */
	if (off < 0)
		off = 0;

	/* Make sure the offset doesn't overflow */
	if (off > (entry->ReservedBlocks*blockSize))
		off = entry->ReservedBlocks*blockSize;

	/* Make sure the read length doesn't overflow */
	if ((off+len) > (entry->ReservedBlocks*blockSize))
		len = (entry->ReservedBlocks*blockSize) - off;

	/* must be able to distinguish between bytes
	 * written and a negative error code */
	if (len > INT_MAX)
		len = INT_MAX;

	/* Skip to the starting block in the disk */
	fseek(disk, (entry->StartingBlock*blockSize) + off, SEEK_SET);

	size_t write_count = fwrite(buf, 1, len, disk);

	entry->FileSize += write_count;

	bmfs_savedir(&dir);

	return write_count;
}

// Write a file to a BMFS volume
void bmfs_writefile(char *filename)
{
	struct BMFSEntry tempentry;
	FILE *tfile;
	int slot, retval;
	unsigned long long tempfilesize;
	char *buffer;

	if (0 == bmfs_findfile(filename, &tempentry, &slot))
	{
		printf("Error: File not found in BMFS. A file entry must first be created.\n");
	}
	else
	{
		if ((tfile = fopen(filename, "rb")) == NULL)
		{
			printf("Error: Could not open local file '%s'\n", tempentry.FileName);
		}
		else
		{
			// Is there enough room in BMFS?
			fseek(tfile, 0, SEEK_END);
			tempfilesize = ftell(tfile);
			rewind(tfile);
			if ((tempentry.ReservedBlocks*blockSize) < tempfilesize)
			{
				printf("Error: Not enough reserved space in BMFS.\n");
			}
			else
			{
				fseek(disk, tempentry.StartingBlock*blockSize, SEEK_SET); // Skip to the starting block in the disk
				buffer = malloc(blockSize);
				if (buffer == NULL)
				{
					printf("Error: Unable to allocate enough memory for buffer.\n");
				}
				else
				{
					while (tempfilesize != 0)
					{
						if (tempfilesize >= blockSize)
						{
							retval = fread(buffer, blockSize, 1, tfile);
							if (retval == 1)
							{
								fwrite(buffer, blockSize, 1, disk);
								tempfilesize -= blockSize;
							}
							else
							{
								printf("Error: Unexpected read length detected.\n");
								tempfilesize = 0;
							}
						}
						else
						{
							retval = fread(buffer, tempfilesize, 1, tfile);
							if (retval == 1)
							{
								memset(buffer+(tempfilesize), 0, (blockSize-tempfilesize)); // 0 the rest of the buffer
								fwrite(buffer, blockSize, 1, disk);
								tempfilesize = 0;
							}
							else
							{
								printf("Error: Unexpected read length detected.\n");
								tempfilesize = 0;
							}
						}
					}
				}
				// Update directory
				tempfilesize = ftell(tfile);
				memcpy(Directory+(slot*64)+48, &tempfilesize, 8);
				fseek(disk, 4096, SEEK_SET);				// Seek 4KiB in for directory
				fwrite(Directory, 4096, 1, disk);			// Write new directory to disk
			}
			fclose(tfile);
		}
	}
}


void bmfs_delete(const char *filename)
{
	struct BMFSDir dir;
	if (bmfs_readdir(&dir, disk) != 0)
		return;

	struct BMFSEntry *entry;
	entry = bmfs_find(&dir, filename);
	if (entry == NULL)
		return;

	entry->FileName[0] = 1;

	bmfs_savedir(&dir);
}


/* EOF */
