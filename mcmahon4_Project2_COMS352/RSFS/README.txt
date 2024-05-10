Andrew McMahon - 468507087

Description of project:
    The RSFS (Ridiculously Simple File System) project involves developing a highly simplified in-memory file system on 
    top of Linux. The goal is to implement basic file management functionalities such as file creation, opening, appending, 
    reading, seeking, writing, cutting, closing, and deletion. The project also includes utilizing given test code to 
    ensure the aforementioned API functions are implemented correctly.

Description of work:
    I personally went in and implemented all of the below functions (as specified in the API) individually, including RSFS_cut
    and RSFS_write (the advanced functionalities). From the given test code's output, my code is correct, and fully functional 
    with no compile time or run time errors.

Files modified (none added, as I was given an API which mentions to simply implement/modify the files as needed):
    RSFS_open: initialized unused open file entry for opened file, allowing either single writer or multiple readers

    RSFS_append: ensured proper appending of content from the buffer to the data blocks of the file, starting from the current end of the file

    RSFS_fseek: implemented logic to change the position of the file with the given file descriptor to the specified offset, ensuring the change is made only if the offset is within the range of the file

    RSFS_read: implemented logic to read data from the file with the given file descriptor to the provided buffer, ensuring that less than the requested size may be read if the file has less data from the current position to its end

    RSFS_close: freed the open file entry associated with the provided file descriptor upon file closure

    RSFS_delete: implemented logic to free the data blocks, inode, and directory entry associated with the provided file name upon file deletion

    RSFS_write: implemented logic to write data from the buffer to the file with the given file descriptor, starting from the current position, and replaced the original content of the file from the current position with the new data

    RSFS_cut: implemented logic to cut up to the specified size of data from the file with the given file descriptor, starting from the current position, and removed the specified amount of data from the file, shifting the subsequent content accordingly