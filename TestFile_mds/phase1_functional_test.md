# Functional Test: Single Client Session

## Objective
Verify that a single client can successfully perform all four operations (UPLOAD, DOWNLOAD, DELETE, LIST) in one session.

## Test Steps

1. **Start the server:**
   ```bash
   ./dropbox_server
   ```

2. **Start the client in another terminal:**
   ```bash
   ./test_client
   ```

3. **Sign up and authenticate:**
   ```
   SIGNUP testuser password123
   ```

4. **Upload a file:**
   - Ensure `upload_test.txt` exists in the client directory.
   ```
   UPLOAD upload_test.txt
   ```

5. **List files:**
   ```
   LIST
   ```
   - Verify that `upload_test.txt` appears in the list.

6. **Download the file:**
   ```
   DOWNLOAD upload_test.txt
   ```
   - Verify that the file is received and matches the original.

7. **Delete the file:**
   ```
   DELETE upload_test.txt
   ```

8. **List files again:**
   ```
   LIST
   ```
   - Verify that `upload_test.txt` is no longer present.

9. **Quit the client:**
   ```
   QUIT
   ```

## Expected Results
- Each command returns a success message.
- File operations (upload, download, delete) work as intended.
- LIST reflects the correct state before and after file operations.
- No errors or crashes occur during the session.
