#include <efi.h>
#include <efilib.h>
#include <kuroko/vm.h>
#include <kuroko/value.h>
#include <kuroko/object.h>
#include <kuroko/memory.h>
#include <kuroko/util.h>

static KrkClass * File = NULL;
static KrkClass * BinaryFile = NULL;

/**
 * @brief Object for a C `FILE*` stream.
 * @extends KrkInstance
 */
struct File {
	KrkInstance inst;
	void * filePtr;
	int type;
	int unowned;
};

#define IS_File(o) (krk_isInstanceOf(o, File))
#define AS_File(o) ((struct File*)AS_OBJECT(o))

#define IS_BinaryFile(o) (krk_isInstanceOf(o, BinaryFile))
#define AS_BinaryFile(o) ((struct File*)AS_OBJECT(o))

static KrkClass * Directory = NULL;
/**
 * @brief OBject for a C `DIR*` stream.
 * @extends KrkInstance
 */
struct Directory {
	KrkInstance inst;
	void * dirPtr;
};

#define IS_Directory(o) (krk_isInstanceOf(o,Directory))
#define AS_Directory(o) ((struct Directory*)AS_OBJECT(o))

#define CURRENT_CTYPE struct File *
#define CURRENT_NAME  self

KRK_METHOD(File,__str__,{
	METHOD_TAKES_NONE();
	KrkValue filename;
	KrkValue modestr;
	if (!krk_tableGet(&self->inst.fields, OBJECT_VAL(S("filename")), &filename) || !IS_STRING(filename)) return krk_runtimeError(vm.exceptions->baseException, "Corrupt File");
	if (!krk_tableGet(&self->inst.fields, OBJECT_VAL(S("modestr")), &modestr) || !IS_STRING(modestr)) return krk_runtimeError(vm.exceptions->baseException, "Corrupt File");
	size_t allocSize = AS_STRING(filename)->length + AS_STRING(modestr)->length + 100;
	char * tmp = malloc(allocSize);
	size_t len = snprintf(tmp, allocSize, "<%s file '%s', mode '%s' at %p>", self->filePtr ? "open" : "closed", AS_CSTRING(filename), AS_CSTRING(modestr), (void*)self);
	KrkString * out = krk_copyString(tmp, len);
	free(tmp);
	return OBJECT_VAL(out);
})

static void makeFileInstance(KrkInstance * module, const char name[], void * file) {
	KrkInstance * fileObject = krk_newInstance(File);
	krk_push(OBJECT_VAL(fileObject));
	KrkValue filename = OBJECT_VAL(krk_copyString(name,strlen(name)));
	krk_push(filename);

	krk_attachNamedValue(&fileObject->fields, "filename", filename);
	((struct File*)fileObject)->filePtr = file;
	((struct File*)fileObject)->unowned = 1;
	((struct File*)fileObject)->type = (file == ST->ConIn) ? 1 :
		(file == ST->ConOut || file == ST->StdErr) ? 2 : 3;

	krk_attachNamedObject(&module->fields, name, (KrkObj*)fileObject);

	krk_pop(); /* filename */
	krk_pop(); /* fileObject */
}

void _createAndBind_fileioMod(void) {
	KrkInstance * module = krk_newInstance(vm.baseClasses->moduleClass);
	krk_attachNamedObject(&vm.modules, "fileio", (KrkObj*)module);
	krk_attachNamedObject(&module->fields, "__name__", (KrkObj*)S("fileio"));
	krk_attachNamedValue(&module->fields, "__file__", NONE_VAL());

	krk_makeClass(module, &File, "File", vm.baseClasses->objectClass);
	KRK_DOC(File,"Interface to a buffered file stream.");
	File->allocSize = sizeof(struct File);
	//File->_ongcsweep = _file_sweep;
	BIND_METHOD(File,__str__);
	krk_defineNative(&File->methods, "__repr__", FUNC_NAME(File,__str__));
	krk_finalizeClass(File);

	makeFileInstance(module, "stdin", ST->ConIn);
	makeFileInstance(module, "stdout", ST->ConOut);
	makeFileInstance(module, "stderr", ST->StdErr);

}

