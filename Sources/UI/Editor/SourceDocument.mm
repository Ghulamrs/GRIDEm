// =============================================================================
//  SourceDocument.mm - see SourceDocument.h
// =============================================================================
#import "SourceDocument.h"
#import "../Common.h"
#import "EditorArea.h"
#include <string>

using GRIDEm::Core::Language;
using GRIDEm::Core::TextFile;

NSString* const GRSourceDocumentStateNotification = @"GRSourceDocumentStateNotification";

static GRIDEm::Settings::GrideSettingsPtr gSettings;
static int gUntitledCounter = 0;

static NSString* NS(const std::string& s) { return GROr([NSString stringWithUTF8String:s.c_str()], @""); }

static std::string Std(NSString* s) { return s.UTF8String ? std::string(s.UTF8String) : std::string(); }

@implementation GRSourceDocument
{
    NSTextStorage* storage;
    NSString* untitledName;
}

+ (void)setSettings:(GRIDEm::Settings::GrideSettingsPtr)settings { gSettings = settings; }
+ (GRIDEm::Settings::GrideSettingsPtr)settings { return gSettings; }

+ (BOOL)autosavesInPlace { return YES; }
// read on the main thread: the text storage belongs to the views
+ (BOOL)canConcurrentlyReadDocumentsOfType:(NSString*)typeName { (void)typeName; return NO; }

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        storage = [[NSTextStorage alloc] init];
        _fileEncoding = GRIDEm::Core::FileEncoding::Utf8;
        _lineEnding = @"LF";        // GRIDEd's new files are CRLF; the Mac's are LF
        _language = Language::C90;
        _projectLanguage = Language::C90;
        _breakpoints = std::make_shared<std::set<int> >();
        self.hasUndoManager = YES;
    }
    return self;
}

+ (GRSourceDocument*)untitledWithLanguage:(Language)language
{
    GRSourceDocument* d = [[GRSourceDocument alloc] init];
    d.language = language;
    d.projectLanguage = language;
    NSString* ext = @".c";
    if (language == Language::Cpp11) ext = @".cpp";
    else if (language == Language::Shalimar) ext = @".shl";
    else if (language == Language::Assembly) ext = @".asm";
    if (gSettings)
    {
        GRIDEm::Settings::LanguageSettingsPtr ls = gSettings->LanguageOf(language);
        if (ls && !ls->Extensions.empty()) ext = NS(ls->Extensions[0]);
        if (ext.length > 0 && ![ext hasPrefix:@"."]) ext = [@"." stringByAppendingString:ext];
    }
    d->untitledName = [NSString stringWithFormat:@"Untitled%d%@", ++gUntitledCounter, ext];
    return d;
}

+ (GRSourceDocument*)openPath:(NSString*)path error:(NSError**)error
{
    GRSourceDocument* d = [[GRSourceDocument alloc] init];
    NSURL* url = [NSURL fileURLWithPath:path];
    if (![d readFromURL:url ofType:@"public.plain-text" error:error]) return nil;
    d.fileURL = url;
    d.fileType = @"public.plain-text";
    d.fileModificationDate = [[[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil] fileModificationDate];
    return d;
}

// ---- properties ----------------------------------------------------------------
- (NSTextStorage*)textStorage { return storage; }

- (NSString*)text { return storage.string; }

- (void)setText:(NSString*)text
{
    NSString* t = GROr(text, @"");
    [storage beginEditing];
    [storage replaceCharactersInRange:NSMakeRange(0, storage.length) withString:t];
    [storage endEditing];
}

- (NSString*)path { return self.fileURL.isFileURL ? self.fileURL.path : nil; }

- (NSString*)displayName
{
    if (self.fileURL != nil) return self.fileURL.lastPathComponent;
    if (untitledName != nil) return untitledName;
    return [super displayName];
}

- (NSString*)encodingName { return NS(TextFile::EncodingLabel(self.fileEncoding)); }

- (BOOL)isModified { return self.isDocumentEdited; }

- (void)detectLanguage
{
    NSString* p = GROr(self.path, untitledName);
    if (p == nil) return;
    if (gSettings) self.language = gSettings->DetectLanguage(Std(p), self.projectLanguage);
}

// ---- reading and writing ----------------------------------------------------------
- (BOOL)readFromData:(NSData*)data ofType:(NSString*)typeName error:(NSError**)outError
{
    (void)typeName;
    (void)outError;
    std::string bytes((const char*)data.bytes, (size_t)data.length);
    GRIDEm::Core::FileEncoding enc = GRIDEm::Core::FileEncoding::Utf8;
    std::string text = TextFile::Decode(bytes, enc);
    self.fileEncoding = enc;
    // GRIDEd: CRLF when the file has it, or has no line end at all; the Mac: LF for the latter
    bool hasCrlf = text.find("\r\n") != std::string::npos;
    bool hasLf = text.find('\n') != std::string::npos;
    self.lineEnding = (hasCrlf || (!hasLf && text.find('\r') != std::string::npos)) ? @"CRLF" : @"LF";
    // "\n" in memory: CRLF and a lone CR both become LF
    std::string plain;
    plain.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] == '\r')
        {
            plain += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') i++;
        }
        else plain += text[i];
    }
    NSString* s = NS(plain);
    if (s == nil) s = @"";
    self.text = s;
    [self.undoManager removeAllActions];
    return YES;
}

- (BOOL)readFromURL:(NSURL*)url ofType:(NSString*)typeName error:(NSError**)outError
{
    NSData* data = [NSData dataWithContentsOfURL:url options:0 error:outError];
    if (data == nil) return NO;
    BOOL ok = [self readFromData:data ofType:typeName error:outError];
    if (ok && url.isFileURL && gSettings)
        self.language = gSettings->DetectLanguage(Std(url.path), self.projectLanguage);
    return ok;
}

- (NSString*)textForDisk
{
    NSString* t = storage.string;
    if ([self.lineEnding isEqualToString:@"CRLF"]) t = [t stringByReplacingOccurrencesOfString:@"\n" withString:@"\r\n"];
    return t;
}

- (NSData*)dataOfType:(NSString*)typeName error:(NSError**)outError
{
    (void)typeName;
    (void)outError;
    std::string bytes = TextFile::Encode(Std([self textForDisk]), self.fileEncoding);
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

- (BOOL)reload:(NSError**)error
{
    if (self.fileURL == nil) return NO;
    BOOL ok = [self revertToContentsOfURL:self.fileURL ofType:GROr(self.fileType, @"public.plain-text") error:error];
    if (ok) [self updateChangeCount:NSChangeCleared];
    return ok;
}

- (BOOL)saveNow:(NSError**)error
{
    if (self.fileURL == nil) return NO;
    NSData* data = [self dataOfType:GROr(self.fileType, @"public.plain-text") error:error];
    if (data == nil) return NO;
    if (![data writeToURL:self.fileURL options:NSDataWritingAtomic error:error]) return NO;
    self.fileModificationDate = [[[NSFileManager defaultManager] attributesOfItemAtPath:self.fileURL.path error:nil]
                                    fileModificationDate];
    [self updateChangeCount:NSChangeCleared];
    return YES;
}

- (void)setFileURL:(NSURL*)url
{
    [super setFileURL:url];
    if (url != nil) untitledName = nil;
    [self detectLanguage];
    [self postState];
}

- (void)postState
{
    [[NSNotificationCenter defaultCenter] postNotificationName:GRSourceDocumentStateNotification object:self];
}

- (void)updateChangeCount:(NSDocumentChangeType)change
{
    BOOL was = self.isDocumentEdited;
    [super updateChangeCount:change];
    if (was != self.isDocumentEdited) [self postState];
}

- (void)setReadOnly:(BOOL)readOnly
{
    _readOnly = readOnly;
    [self postState];
}

- (void)setPinned:(BOOL)pinned
{
    _pinned = pinned;
    [self postState];
}

// a read-only file (CCS / VS project) is never written
- (BOOL)isInViewingMode { return self.readOnly || [super isInViewingMode]; }

- (void)makeWindowControllers
{
    // no window of its own: GREditorArea shows the document in the main window
}
@end
