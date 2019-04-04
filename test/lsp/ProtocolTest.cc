#include "test/lsp/ProtocolTest.h"
#include "test/helpers/lsp.h"
#include "test/helpers/position_assertions.h"

namespace sorbet::test::lsp {
using namespace std;

MockFileSystem::MockFileSystem(std::string_view rootPath) : rootPath(string(rootPath)) {}

string makeAbsolute(string_view rootPath, string_view path) {
    if (path[0] == '/') {
        return string(path);
    } else {
        return fmt::format("{}/{}", rootPath, path);
    }
}

void MockFileSystem::writeFiles(const vector<pair<string, string>> &initialFiles) {
    for (auto &pair : initialFiles) {
        writeFile(pair.first, pair.second);
    }
}

string MockFileSystem::readFile(string_view path) const {
    auto file = contents.find(makeAbsolute(rootPath, path));
    if (file == contents.end()) {
        throw FileNotFoundException();
    } else {
        return file->second;
    }
}

void MockFileSystem::writeFile(string_view filename, string_view text) {
    contents[makeAbsolute(rootPath, filename)] = text;
}

void MockFileSystem::deleteFile(string_view filename) {
    auto file = contents.find(makeAbsolute(rootPath, filename));
    if (file == contents.end()) {
        throw FileNotFoundException();
    } else {
        contents.erase(file);
    }
}

vector<string> MockFileSystem::listFilesInDir(string_view path, UnorderedSet<string> extensions, bool recursive,
                                              const std::vector<std::string> &absoluteIgnorePatterns,
                                              const std::vector<std::string> &relativeIgnorePatterns) const {
    Exception::raise("Not implemented.");
}

void ProtocolTest::SetUp() {
    rootPath = "/Users/jvilk/stripe/pay-server";
    rootUri = fmt::format("file://{}", rootPath);
    fs = make_shared<MockFileSystem>(rootPath);
    // Always use fast path
    // TODO: Make toggleable so we can run slow path tests?
    bool disableFastPath = false;
    lspWrapper = make_unique<LSPWrapper>(rootPath, disableFastPath);
    lspWrapper->opts.fs = fs;
    lspWrapper->enableAllExperimentalFeatures();
}

vector<unique_ptr<LSPMessage>> ProtocolTest::initializeLSP() {
    auto responses = sorbet::test::initializeLSP(rootPath, rootUri, *lspWrapper, nextId);
    updateDiagnostics(responses);
    return responses;
}

string ProtocolTest::getUri(string_view filePath) {
    return filePathToUri(rootUri, filePath);
}

unique_ptr<LSPMessage> ProtocolTest::openFile(string_view path, string_view contents) {
    sourceFileContents[string(path)] =
        make_shared<core::File>(string(path), string(contents), core::File::Type::Normal);
    auto uri = getUri(path);
    auto didOpenParams =
        make_unique<DidOpenTextDocumentParams>(make_unique<TextDocumentItem>(uri, "ruby", 1, string(contents)));
    auto didOpenNotif = make_unique<NotificationMessage>("2.0", "textDocument/didOpen");
    didOpenNotif->params = didOpenParams->toJSONValue(lspWrapper->alloc);
    return make_unique<LSPMessage>(move(didOpenNotif));
}

unique_ptr<LSPMessage> ProtocolTest::closeFile(string_view path) {
    // File is closed, so update contents from mock FS.
    try {
        sourceFileContents[string(path)] =
            make_shared<core::File>(string(path), string(fs->readFile(path)), core::File::Type::Normal);
    } catch (FileNotFoundException e) {
        auto it = sourceFileContents.find(path);
        if (it != sourceFileContents.end()) {
            sourceFileContents.erase(it);
        }
    }

    auto uri = getUri(path);
    auto didCloseParams = make_unique<DidCloseTextDocumentParams>(make_unique<TextDocumentIdentifier>(uri));
    auto didCloseNotif = make_unique<NotificationMessage>("2.0", "textDocument/didClose");
    didCloseNotif->params = didCloseParams->toJSONValue(lspWrapper->alloc);
    return make_unique<LSPMessage>(move(didCloseNotif));
}

unique_ptr<LSPMessage> ProtocolTest::changeFile(string_view path, string_view newContents, int version) {
    sourceFileContents[string(path)] =
        make_shared<core::File>(string(path), string(newContents), core::File::Type::Normal);
    auto uri = getUri(path);
    auto textDocIdent = make_unique<VersionedTextDocumentIdentifier>(uri, version);
    vector<unique_ptr<TextDocumentContentChangeEvent>> changeEvents;
    changeEvents.push_back(make_unique<TextDocumentContentChangeEvent>(string(newContents)));
    auto didChangeParams = make_unique<DidChangeTextDocumentParams>(move(textDocIdent), move(changeEvents));
    auto didChangeNotif = make_unique<NotificationMessage>("2.0", "textDocument/didChange");
    didChangeNotif->params = didChangeParams->toJSONValue(lspWrapper->alloc);
    return make_unique<LSPMessage>(move(didChangeNotif));
}

unique_ptr<LSPMessage> ProtocolTest::documentSymbol(string_view path) {
    auto docSymParams = make_unique<DocumentSymbolParams>(make_unique<TextDocumentIdentifier>(getUri(path)));
    auto req = make_unique<RequestMessage>("2.0", nextId++, "textDocument/documentSymbol");
    req->params = docSymParams->toJSONValue(lspWrapper->alloc);
    return make_unique<LSPMessage>(move(req));
}

unique_ptr<LSPMessage> ProtocolTest::getDefinition(string_view path, int line, int character) {
    return makeDefinitionRequest(lspWrapper->alloc, nextId++, getUri(path), line, character);
}

unique_ptr<LSPMessage> ProtocolTest::watchmanFileUpdate(vector<string> updatedFilePaths) {
    auto req = make_unique<RequestMessage>("2.0", nextId++, "sorbet/watchmanFileChange");
    req->params = WatchmanQueryResponse("", "", false, updatedFilePaths).toJSONValue(lspWrapper->alloc);
    return make_unique<LSPMessage>(move(req));
}

void ProtocolTest::writeFilesToFS(vector<pair<string, string>> files) {
    for (auto &file : files) {
        sourceFileContents[file.first] =
            make_shared<core::File>(string(file.first), string(file.second), core::File::Type::Normal);
    }
    fs->writeFiles(files);
}

void ProtocolTest::deleteFileFromFS(string_view filename) {
    auto it = sourceFileContents.find(filename);
    if (it != sourceFileContents.end()) {
        sourceFileContents.erase(it);
    }
    fs->deleteFile(filename);
}

unique_ptr<LSPMessage> ProtocolTest::cancelRequest(int id) {
    return makeRequestMessage(lspWrapper->alloc, "$/cancelRequest", nextId++, CancelParams(id));
}

vector<unique_ptr<LSPMessage>> ProtocolTest::send(const LSPMessage &message) {
    auto responses = lspWrapper->getLSPResponsesFor(message);
    updateDiagnostics(responses);
    return responses;
}

vector<unique_ptr<LSPMessage>> ProtocolTest::send(vector<unique_ptr<LSPMessage>> messages) {
    auto responses = lspWrapper->getLSPResponsesFor(messages);
    updateDiagnostics(responses);
    return responses;
}

void ProtocolTest::updateDiagnostics(const vector<unique_ptr<LSPMessage>> &messages) {
    for (auto &msg : messages) {
        if (msg->isNotification() && msg->method() == "textDocument/publishDiagnostics") {
            if (auto diagnosticParams = getPublishDiagnosticParams(lspWrapper->alloc, msg->asNotification())) {
                // Will explicitly overwrite older diagnostics that are irrelevant.
                diagnostics[uriToFilePath(rootUri, (*diagnosticParams)->uri)] = move((*diagnosticParams)->diagnostics);
            }
        }
    }
}

void ProtocolTest::assertDiagnostics(vector<unique_ptr<LSPMessage>> messages, vector<ExpectedDiagnostic> expected) {
    for (auto &msg : messages) {
        if (!assertNotificationMessage("textDocument/publishDiagnostics", *msg)) {
            // Assertion failed: Received a non-diagnostic. No need to continue.
            return;
        }
    }

    // Convert ExpectedDiagnostic into ErrorAssertion objects.
    vector<shared_ptr<ErrorAssertion>> errorAssertions;
    for (auto e : expected) {
        auto range = RangeAssertion::makeRange(e.line);
        errorAssertions.push_back(ErrorAssertion::make(e.path, range, e.line, e.message));
    }

    // Use same logic as main test runner.
    ErrorAssertion::checkAll(sourceFileContents, errorAssertions, diagnostics);
}

} // namespace sorbet::test::lsp