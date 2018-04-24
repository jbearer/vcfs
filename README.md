# About
Often users work on multiple computers but want access to the same files, such as static configuration files. Other times, an organization may want to share such files among many members. VCFS is a distributed file system which supports the sharing of read-only or rarely-written files between computers and across different users within an organization.

# Set up:
0) Download repository and ensure the cli and client folders are on your path. Make the client.
1) On your server set up by running: vcfs-serve <repo>
2) On your client (ie local computer): vcfs-mount <mnt> <remote> <ip> <port>
   Note: port is defaulted to 9091 in server setup but may be modified by setting VCFS_CLIENT_PORT environment variable on server setup
3) To share files (files are *not* shared by default): vcfs-add <file>
4) In the event of a conflict use vcfs-merge to resolve the conflict
