#ifndef UTILS_H
#define UTILS_H

enum Request {
    RequestNone,
    RequestSignIn,
    RequestSignUp,
    RequestSignOut,
    RequestGetData,
    RequestDelete,
    RequestAddFolder,
    RequestRenameFolder,
    RequestAddFile,
    RequestRenameFile,
    RequestDownload,
};

enum Response {
    ResponseNone,
    ResponseSignInSuccess,
    ResponseSignInError,
    ResponseSignUpSuccess,
    ResponseSignUpError,
    ResponseSignOutSuccess,
    ResponseSignOutError,
    ResponseGetDataSuccess,
    ResponseGetDataError,
    ResponseDeleteSuccess,
    ResponseDeleteError,
    ResponseAddFolderSuccess,
    ResponseAddFolderError,
    ResponseRenameFolderSuccess,
    ResponseRenameFolderError,
    ResponseAddFileSuccess,
    ResponseAddFileError,
    ResponseRenameFileSuccess,
    ResponseRenameFileError,
    ResponseDownloadSuccess,
    ResponseDownloadError,
    ResponseSuccess,
    ResponseError,
};

#endif // !UTILS_H
