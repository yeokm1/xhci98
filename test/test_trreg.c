/*
 * test_trreg.c - XHCITR's listing of a USB device's driver key
 * (xhcitr\tr_reg.c, design record 25 section 25, task 50.29): every value
 * type's text, at each limit and past it (a string's 512 characters, a
 * multi-string's 32 strings and 2048 characters, 256 bytes of binary data,
 * a name's 128 characters, a key's 256 values and 64 KB), a DWORD and a
 * QWORD of the wrong size, the four-byte binary number 98 SE keeps shown
 * as bytes, a string with no NUL and one with data after it, the unnamed
 * value; over a modelled RegEnumValueA: a value that grows between the
 * size query and the read (once, and at every read), one larger than the
 * read, one gone between the calls, a key that does not open, an
 * enumeration error, the count of the values past the limits (clean, a
 * name too long for the buffer, an error, the count's own bound), a value
 * of exactly the read's 65536 bytes and one more; the rule for names that
 * suggest a secret, read against a guard page so a read past a name's NUL
 * faults, short names and a name of the registry's 16383 characters; the
 * device report's screening (shown, withheld, not screened), a name in the
 * system's code page and in UTF-16 at either alignment screened before any
 * byte is replaced, and the screen's overlapping windows; quotes doubled;
 * and the walk over one device's devnodes: nested functions, HID devnodes
 * below an interface, the depth and devnode limits, a hub, devnodes not
 * all read, a stopped output, and every devnode's key listed through the
 * walk.
 */

#include <stdio.h>
#include <string.h>
#include "../xhcitr/tr_reg.h"
#include "test_harness.h"

static char textBuf[TR_REG_KEY_TEXT * 2];
static char plain[TR_REG_KEY_TEXT * 2];
static unsigned char dataBuf[TR_REG_READ_BYTES];
static char nameBuf[TR_REG_NAME_CHARS];
static char projBuf[TR_REG_PROJ_BYTES];
static unsigned char pattern[80000];

/* The text with its CRs left out. */
static const char *flat(const TR_TEXT *t)
{
    size_t i;
    size_t n = 0;

    for (i = 0; i < t->Len && n + 1 < sizeof(plain); i++) {
        if (t->Buf[i] != '\r') {
            plain[n++] = t->Buf[i];
        }
    }
    plain[n] = '\0';
    return plain;
}

static void check_text(const TR_TEXT *t, const char *want, const char *what,
                       int line)
{
    const char *got = flat(t);
    int ok = strcmp(got, want) == 0;

    check_impl(ok, what, __FILE__, line);
    if (!ok) {
        printf("---- got:\n%s---- want:\n%s----\n", got, want);
    }
}

static void check_has(const TR_TEXT *t, const char *want, const char *what,
                      int line)
{
    const char *got = flat(t);
    int ok = strstr(got, want) != NULL;

    check_impl(ok, what, __FILE__, line);
    if (!ok) {
        printf("---- got:\n%s---- wanted in it:\n%s\n----\n", got, want);
    }
}

#define TEXT_IS(t, want, what) check_text((t), (want), (what), __LINE__)
#define TEXT_HAS(t, want, what) check_has((t), (want), (what), __LINE__)

static void one(TR_TEXT *t, const char *name, TR_U32 type, const void *data,
                TR_U32 bytes)
{
    tr_text_init(t, textBuf, sizeof(textBuf));
    tr_reg_value_text(t, "  ", name, type, (const unsigned char *)data, bytes,
                      NULL, NULL);
}

/* ---- the modelled key ---------------------------------------------------- */

typedef struct _MV {
    const char *Name;
    TR_U32 Type;
    const unsigned char *Data;
    TR_U32 Bytes;
    const TR_U32 *Sizes;        /* the size at each call, then Bytes */
    int SizeCount;
    const char *After;          /* non-NULL: the name handed back with no
                                 * NUL, these letters after its length and
                                 * the rest of the buffer 'a' */
} MV;

typedef struct _MODEL {
    MV *V;
    int Count;
    int ErrorIndex;             /* -1: none */
    TR_U32 Error;
    int GoneIndex;              /* after its first call, absent */
    TR_U32 Endless;             /* past Count, DWORD values "V" up to here */
    int SizeCall[300];
    int Calls;
    int Gone;
} MODEL;

static TR_U32 model_enum(void *context, TR_U32 index, char *name,
                         TR_U32 *nameChars, TR_U32 *type,
                         unsigned char *data, TR_U32 *bytes)
{
    MODEL *m = (MODEL *)context;
    MV *v;
    TR_U32 size;
    size_t n;

    m->Calls++;
    if ((int)index == m->ErrorIndex) {
        return m->Error;
    }
    if ((int)index == m->GoneIndex && m->Gone) {
        return TR_REG_NO_MORE;
    }
    if ((int)index >= m->Count) {
        static const unsigned char one[4] = { 1, 0, 0, 0 };
        if (index >= m->Endless) {
            return TR_REG_NO_MORE;
        }
        if (*nameChars < 2) {
            *nameChars = 2;
            return TR_REG_MORE_DATA;
        }
        strcpy(name, "V");
        *nameChars = 1;
        *type = TR_REG_DWORD;
        if (data != NULL) {
            if (*bytes < 4) {
                *bytes = 4;
                return TR_REG_MORE_DATA;
            }
            memcpy(data, one, 4);
        }
        *bytes = 4;
        return TR_REG_OK;
    }
    if ((int)index == m->GoneIndex) {
        m->Gone = 1;
    }
    v = &m->V[index];
    size = v->Bytes;
    if (v->Sizes != NULL && index < 300 &&
        m->SizeCall[index] < v->SizeCount) {
        size = v->Sizes[m->SizeCall[index]++];
    }
    n = strlen(v->Name);
    if (n + 1 > *nameChars) {
        *nameChars = (TR_U32)n + 1;
        return TR_REG_MORE_DATA;
    }
    if (v->After != NULL) {
        size_t a = strlen(v->After);

        memset(name, 'a', *nameChars);
        memcpy(name, v->Name, n);
        memcpy(name + n, v->After, n + a <= *nameChars ? a : *nameChars - n);
    } else {
        memcpy(name, v->Name, n + 1);
    }
    *nameChars = (TR_U32)n;
    *type = v->Type;
    if (data == NULL) {
        *bytes = size;
        return TR_REG_OK;
    }
    if (size > *bytes) {
        *bytes = size;
        return TR_REG_MORE_DATA;
    }
    memcpy(data, v->Data != NULL ? v->Data : pattern, size);
    *bytes = size;
    return TR_REG_OK;
}

static int keyWidth;    /* the notes' width (task 50.39), 0 the default */

static int key(TR_TEXT *t, MODEL *m, TR_U32 openError, const TR_REG_SCR *scr)
{
    TR_REG_KEY k;

    memset(&k, 0, sizeof(k));
    k.Width = keyWidth;
    k.OpenError = openError;
    k.Enum = model_enum;
    k.Context = m;
    k.Data = dataBuf;
    k.Name = nameBuf;
    k.Proj = projBuf;
    k.Scr = scr;
    tr_text_init(t, textBuf, sizeof(textBuf));
    return tr_reg_key_text(t, "  ", &k);
}

static void model_init(MODEL *m, MV *v, int count)
{
    memset(m, 0, sizeof(*m));
    m->V = v;
    m->Count = count;
    m->ErrorIndex = -1;
    m->GoneIndex = -1;
}

/* The device report's screen: "HOSTA" is this machine's name. */
static int screen_names(const char *text, void *context)
{
    const char *s;

    (void)context;
    for (s = text; *s != '\0'; s++) {
        if (strncmp(s, "HOSTA", 5) == 0) {
            return TR_REG_WITHHOLD;
        }
    }
    return TR_REG_SHOW;
}

static int screen_none(const char *text, void *context)
{
    (void)text;
    (void)context;
    return TR_REG_UNSCREENED;
}

static const TR_REG_SCR scrNames = { screen_names, NULL, NULL };
static const TR_REG_SCR scrNone = { screen_none, NULL, NULL };

/* ---- the vectors --------------------------------------------------------- */

/* Section 25 item 3's table (the owner's decision of 2026-11-17): 1 the
 * data withheld, 0 shown. A term counts at a word's start, or anywhere in
 * a word of capitals alone, when the name ends after it, an S, characters
 * that are not letters and a word of a secret's form (HASH, HASHES, HEX,
 * VALUE, MATERIAL, LIST); KEY, WPAPSK and PIN CODE (characters that are
 * not letters between) are terms; HotKey and ShortcutKey are harmless. */
static const struct {
    const char *Name;
    int Withheld;
} nameTable[] = {
    { "Password", 1 }, { "UserPassword", 1 }, { "TrkPassword", 1 },
    { "Passwords", 1 }, { "PassWord", 1 }, { "USERPASSWORD", 1 },
    { "password_hash", 1 }, { "PasswordHash", 1 }, { "PASSWORDHASH", 1 },
    { "PasswordLength", 0 }, { "PasswordHashes", 1 },
    { "PasswordHashes1", 1 }, { "PasswordHash1", 1 }, { "PasswordHashed", 0 },
    { "Pass_word", 0 }, { "PasswordString", 0 }, { "KeyData", 0 },
    { "admin_pwd", 1 }, { "AdminPwd", 1 }, { "PASSPHRASE", 1 },
    { "WpaPassphraseHex", 1 }, { "Secret", 1 }, { "ClientSecret", 1 },
    { "SecretCount", 0 }, { "UserCredential", 1 }, { "Credentials", 1 },
    { "WPAPSK", 1 }, { "WpaPsk", 1 }, { "WPAPsk", 1 }, { "WPAPskLength", 0 },
    { "WPAPSKLength", 0 }, { "PskValue", 1 }, { "AuthToken", 1 },
    { "TokenCount", 0 }, { "TokenErrors", 0 }, { "TokenList", 1 },
    { "License", 1 }, { "LicenseKey", 1 }, { "LicenseType", 0 },
    { "ProductKey", 1 }, { "Product Key", 1 }, { "CdKey", 1 },
    { "WepKey1", 1 }, { "WEPKEY", 1 }, { "Wep_Key_2", 1 }, { "WpaKey", 1 },
    { "NetworkKey", 1 }, { "Network Key", 1 }, { "WirelessNetworkKey", 1 },
    { "DefaultKey", 1 }, { "PreSharedKey", 1 }, { "SharedKey", 1 },
    { "PassKey", 1 }, { "EncryptionKey", 1 }, { "PrivateKey", 1 },
    { "AuthKey", 1 }, { "ApiKey", 1 }, { "SessionKey", 1 }, { "TrkKey1", 1 },
    { "my-key", 1 }, { "Key", 1 }, { "KEY", 1 }, { "Key0", 1 },
    { "KEY_1", 1 }, { "Keys", 1 }, { "KeyMaterial", 1 }, { "Key1Value", 1 },
    { "KeyValue", 1 }, { "KeyHexagon", 0 }, { "HotKey", 0 }, { "HotKeys", 0 },
    { "HOTKEY", 0 }, { "ShortcutKey", 0 }, { "HotKey1", 1 },
    { "KeyIndex", 0 }, { "DefaultKeyId", 0 }, { "WepKeyLength", 0 },
    { "K_e_y", 0 }, { "K1E2Y", 0 }, { "K_E_Y", 0 }, { "K e y", 0 },
    { "Keyboard", 0 }, { "KeyboardDataQueueSize", 0 }, { "Monkey", 0 },
    { "Turkey", 0 }, { "MONKEY", 1 }, { "TURKEY", 1 }, { "PIN", 1 },
    { "UserPin", 1 }, { "PinCode", 1 }, { "Pin Code", 1 }, { "Pin_Code", 1 },
    { "Pin-Code2", 1 }, { "Pin  Code", 1 }, { "Pin Codes", 1 },
    { "Pin Codec", 0 }, { "PinCodeLength", 0 }, { "Pin CodeLength", 0 },
    { "Spin Code", 0 }, { "Pinned", 0 }, { "Spindown", 0 }, { "SpinUp", 0 },
    { "SpinValue", 0 }, { "SSID", 1 }, { "DesiredSSID", 1 }, { "Ssid2", 1 },
    { "SsidList", 1 }, { "SSIDList", 1 }, { "SsidLists", 1 },
    { "*SSIdleTimeout", 0 }, { "*SSIdleTimeoutScreenOff", 0 },
    { "*PMWiFiRekeyOffload", 0 }, { "*SelectiveSuspend", 0 },
    { "NetworkAddress", 0 }, { "TrkNetworkAddress", 0 }, { "DriverDesc", 0 },
    { "MatchingDeviceId", 0 }, { "InfPath", 0 }, { "DriverVersion", 0 },
    { "XhciLinkKeep", 0 }, { "Uac2UsbStats", 0 },
    { "SetupPreferredAudioDevices", 0 }, { "", 0 },
    { NULL, 0 }
};

static void test_names(void)
{
    char what[80];
    int i;

    for (i = 0; nameTable[i].Name != NULL; i++) {
        sprintf(what, "\"%s\" %s", nameTable[i].Name,
                nameTable[i].Withheld ? "withheld" : "shown");
        CHECK(tr_reg_secret_name(nameTable[i].Name) == nameTable[i].Withheld,
              what);
    }
    CHECK(!tr_reg_secret_name(NULL), "NULL name");
}

static void test_types(void)
{
    static const unsigned char dw[4] = { 0x78, 0x56, 0x34, 0x12 };
    static const unsigned char be[4] = { 0x12, 0x34, 0x56, 0x78 };
    static const unsigned char qw[8] = { 0, 0, 0, 0, 1, 0, 0, 0 };
    static const unsigned char qmax[8] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    static const unsigned char q0[8] = { 0 };
    static const unsigned char bin4[4] = { 0x01, 0x00, 0x00, 0x00 };
    static const char msz[] = "one\0two\0three\0";
    TR_TEXT t;
    unsigned char b17[17];
    int i;

    for (i = 0; i < 17; i++) {
        b17[i] = (unsigned char)(0xF0 + i);
    }
    one(&t, "DriverDesc", TR_REG_SZ, "USB Input Device", 17);
    TEXT_IS(&t, "  \"DriverDesc\" = REG_SZ \"USB Input Device\"\n", "REG_SZ");
    one(&t, "Path", TR_REG_EXPAND_SZ, "%SystemRoot%\\x.dll", 19);
    TEXT_IS(&t, "  \"Path\" = REG_EXPAND_SZ \"%SystemRoot%\\x.dll\"\n",
            "REG_EXPAND_SZ, not expanded");
    one(&t, "TestMulti", TR_REG_MULTI_SZ, msz, sizeof(msz));
    TEXT_IS(&t, "  \"TestMulti\" = REG_MULTI_SZ, 3 strings:\n"
                "      \"one\"\n      \"two\"\n      \"three\"\n",
            "REG_MULTI_SZ");
    one(&t, "Empty", TR_REG_MULTI_SZ, "\0", 2);
    TEXT_IS(&t, "  \"Empty\" = REG_MULTI_SZ, no strings\n",
            "an empty REG_MULTI_SZ");
    one(&t, "TestDword", TR_REG_DWORD, dw, 4);
    TEXT_IS(&t, "  \"TestDword\" = REG_DWORD 305419896 (0x12345678)\n",
            "REG_DWORD");
    one(&t, "Big", TR_REG_DWORD_BIG_ENDIAN, be, 4);
    TEXT_IS(&t, "  \"Big\" = REG_DWORD_BIG_ENDIAN 305419896 (0x12345678)\n",
            "REG_DWORD_BIG_ENDIAN");
    one(&t, "Q", TR_REG_QWORD, qw, 8);
    TEXT_IS(&t, "  \"Q\" = REG_QWORD 4294967296 (0x0000000100000000)\n",
            "REG_QWORD 2^32");
    one(&t, "Q", TR_REG_QWORD, qmax, 8);
    TEXT_IS(&t, "  \"Q\" = REG_QWORD 18446744073709551615 "
                "(0xFFFFFFFFFFFFFFFF)\n", "REG_QWORD all ones");
    one(&t, "Q", TR_REG_QWORD, q0, 8);
    TEXT_IS(&t, "  \"Q\" = REG_QWORD 0 (0x0000000000000000)\n",
            "REG_QWORD zero");
    one(&t, "TestBin4", TR_REG_BINARY, bin4, 4);
    TEXT_IS(&t, "  \"TestBin4\" = REG_BINARY, 4 bytes: 01 00 00 00\n",
            "98 SE's four-byte binary number, shown as bytes");
    one(&t, "Nothing", TR_REG_BINARY, bin4, 0);
    TEXT_IS(&t, "  \"Nothing\" = REG_BINARY, 0 bytes\n", "empty binary");
    one(&t, "One", TR_REG_BINARY, bin4, 1);
    TEXT_IS(&t, "  \"One\" = REG_BINARY, 1 byte: 01\n", "one byte");
    one(&t, "B17", TR_REG_BINARY, b17, 17);
    TEXT_IS(&t, "  \"B17\" = REG_BINARY, 17 bytes:\n"
                "      0000  F0 F1 F2 F3 F4 F5 F6 F7 F8 F9 FA FB FC FD FE FF\n"
                "      0010  00\n", "17 bytes on offset lines");
    one(&t, "N", TR_REG_NONE, bin4, 2);
    TEXT_IS(&t, "  \"N\" = REG_NONE, 2 bytes: 01 00\n", "REG_NONE");
    one(&t, "L", TR_REG_LINK, bin4, 2);
    TEXT_IS(&t, "  \"L\" = REG_LINK, 2 bytes: 01 00\n", "REG_LINK");
    one(&t, "R", TR_REG_RESOURCE_LIST, bin4, 2);
    TEXT_HAS(&t, "= REG_RESOURCE_LIST, 2 bytes: 01 00", "REG_RESOURCE_LIST");
    one(&t, "R", TR_REG_FULL_RESOURCE, bin4, 2);
    TEXT_HAS(&t, "= REG_FULL_RESOURCE_DESCRIPTOR, 2 bytes", "full resource");
    one(&t, "R", TR_REG_RESOURCE_REQ, bin4, 2);
    TEXT_HAS(&t, "= REG_RESOURCE_REQUIREMENTS_LIST, 2 bytes", "requirements");
    one(&t, "U", 42UL, bin4, 3);
    TEXT_IS(&t, "  \"U\" = type 0x0000002A, 3 bytes: 01 00 00\n",
            "an unknown type");
    one(&t, "D3", TR_REG_DWORD, dw, 3);
    TEXT_IS(&t, "  \"D3\" = REG_DWORD, 3 bytes (not 4): 78 56 34\n",
            "a DWORD of 3 bytes");
    one(&t, "Q4", TR_REG_QWORD, dw, 4);
    TEXT_IS(&t, "  \"Q4\" = REG_QWORD, 4 bytes (not 8): 78 56 34 12\n",
            "a QWORD of 4 bytes");
    one(&t, "", TR_REG_SZ, "x", 2);
    TEXT_IS(&t, "  (default) = REG_SZ \"x\"\n", "the unnamed value");
    one(&t, "WepKey1", TR_REG_SZ, "0123456789", 11);
    TEXT_IS(&t, "  \"WepKey1\" = REG_SZ, 11 bytes: withheld (its name "
                "suggests a key or password)\n", "a secret's data withheld");
    one(&t, "PinCode", TR_REG_DWORD, dw, 4);
    TEXT_HAS(&t, "withheld (its name suggests", "a secret's number too");
}

static void test_strings(void)
{
    static char s[700];
    TR_TEXT t;
    char want[800];

    memset(s, 'a', sizeof(s));
    s[512] = '\0';
    one(&t, "S", TR_REG_SZ, s, 513);
    CHECK(strstr(flat(&t), "...") == NULL && strlen(flat(&t)) == 2 + 3 + 3 +
          7 + 512 + 2 + 1, "512 characters shown whole");
    memset(s, 'a', sizeof(s));
    s[513] = '\0';
    one(&t, "S", TR_REG_SZ, s, 514);
    TEXT_HAS(&t, "...\" (the first 512 of 513 characters)\n",
             "513 characters cut");
    one(&t, "NoNul", TR_REG_SZ, "abcdef", 3);
    TEXT_IS(&t, "  \"NoNul\" = REG_SZ \"abc\"\n", "a string with no NUL");
    one(&t, "After", TR_REG_SZ, "ab\0cd", 6);
    TEXT_IS(&t, "  \"After\" = REG_SZ \"ab\"\n", "data after the NUL");
    one(&t, "Odd", TR_REG_SZ, "a\tb\x80" "c", 6);
    TEXT_IS(&t, "  \"Odd\" = REG_SZ \"a?b?c\"\n", "non-printable as ?");
    one(&t, "Zero", TR_REG_SZ, "", 0);
    TEXT_IS(&t, "  \"Zero\" = REG_SZ \"\"\n", "a string of 0 bytes");
    memset(s, 'n', 200);
    s[200] = '\0';
    one(&t, s, TR_REG_SZ, "v", 2);
    memset(want, 'n', 128);
    want[128] = '\0';
    TEXT_HAS(&t, want, "a long name's first 128 characters");
    TEXT_HAS(&t, "...\" (a name of 200 characters) = REG_SZ \"v\"",
             "a long name cut");
}

static void test_multi(void)
{
    static char m[40000];
    TR_TEXT t;
    size_t at;
    int i;

    at = 0;
    for (i = 0; i < 32; i++) {
        m[at++] = (char)('A' + (i % 26));
        m[at++] = '\0';
    }
    m[at++] = '\0';
    one(&t, "M", TR_REG_MULTI_SZ, m, (TR_U32)at);
    TEXT_HAS(&t, "32 strings:", "32 strings counted");
    CHECK(strstr(flat(&t), "more string") == NULL, "32 strings all shown");
    at--;
    m[at++] = 'Z';
    m[at++] = '\0';
    m[at++] = '\0';
    one(&t, "M", TR_REG_MULTI_SZ, m, (TR_U32)at);
    TEXT_HAS(&t, "33 strings:", "33 strings counted");
    TEXT_HAS(&t, "      (1 more string not shown)\n", "the 33rd not shown");
    at = 0;
    for (i = 0; i < 5; i++) {
        memset(m + at, 'a' + i, 512);
        at += 512;
        m[at++] = '\0';
    }
    m[at++] = '\0';
    one(&t, "M", TR_REG_MULTI_SZ, m, (TR_U32)at);
    TEXT_HAS(&t, "5 strings:", "five long strings counted");
    TEXT_HAS(&t, "      (1 more string not shown)\n",
             "2048 characters: the fifth not shown");
    at = 0;
    memset(m, 'x', 600);
    at = 600;
    m[at++] = '\0';
    m[at++] = '\0';
    one(&t, "M", TR_REG_MULTI_SZ, m, (TR_U32)at);
    TEXT_HAS(&t, "...\" (the first 512 of 600 characters)",
             "a multi-string's long string cut");
    /* No closing NUL at all: the strings end with the data. */
    one(&t, "M", TR_REG_MULTI_SZ, "ab\0cd", 5);
    TEXT_IS(&t, "  \"M\" = REG_MULTI_SZ, 2 strings:\n      \"ab\"\n"
                "      \"cd\"\n", "a multi-string with no closing NUL");
}

static void test_hex_limit(void)
{
    TR_TEXT t;
    const char *f;

    one(&t, "TestBin400", TR_REG_BINARY, pattern, 400);
    f = flat(&t);
    TEXT_HAS(&t, "  \"TestBin400\" = REG_BINARY, 400 bytes, the first 256 "
                 "shown:\n      0000  00 01 02", "400 bytes, 256 shown");
    TEXT_HAS(&t, "      00F0  F0 F1 F2 F3 F4 F5 F6 F7 F8 F9 FA FB FC FD FE FF\n",
             "the last line shown");
    CHECK(strstr(f, "0100  ") == NULL, "nothing past 256 bytes");
    one(&t, "TestBin256", TR_REG_BINARY, pattern, 256);
    TEXT_HAS(&t, "256 bytes:\n", "256 bytes whole");
}

static void test_key(void)
{
    static const unsigned char dw[4] = { 1, 0, 0, 0 };
    static MV many[300];
    static const TR_U32 growOnce[] = { 70000UL, 60000UL, 70000UL, 60000UL,
                                       60500UL };
    static const TR_U32 growEvery[] = { 70000UL, 60000UL, 70000UL, 60000UL,
                                        70000UL, 60000UL, 70000UL };
    MV v[4];
    MODEL m;
    TR_TEXT t;
    int n;
    int i;

    /* A key that does not open. */
    model_init(&m, v, 0);
    n = key(&t, &m, 2, NULL);
    TEXT_IS(&t, "  not read: the key did not open (error 2)\n",
            "an unreadable key");
    CHECK_EQ(n, 0, "none listed");
    CHECK_EQ(m.Calls, 0, "no read of a key not opened");

    /* No values. */
    model_init(&m, v, 0);
    n = key(&t, &m, 0, NULL);
    TEXT_IS(&t, "  no values\n", "an empty key");

    /* Two values, then an enumeration error. */
    memset(v, 0, sizeof(v));
    v[0].Name = "A";
    v[0].Type = TR_REG_DWORD;
    v[0].Data = dw;
    v[0].Bytes = 4;
    v[1].Name = "B";
    v[1].Type = TR_REG_SZ;
    v[1].Data = (const unsigned char *)"b";
    v[1].Bytes = 2;
    v[2] = v[1];
    model_init(&m, v, 3);
    m.ErrorIndex = 2;
    m.Error = 5;
    n = key(&t, &m, 0, NULL);
    TEXT_IS(&t, "  \"A\" = REG_DWORD 1 (0x00000001)\n  \"B\" = REG_SZ \"b\"\n"
                "  (the reading stopped at value 2: error 5)\n  2 values\n",
            "an enumeration error");
    CHECK_EQ(n, 2, "two listed");

    /* Larger than the read. */
    memset(v, 0, sizeof(v));
    v[0].Name = "Huge";
    v[0].Type = TR_REG_BINARY;
    v[0].Bytes = 70000UL;
    v[1].Name = "After";
    v[1].Type = TR_REG_DWORD;
    v[1].Data = dw;
    v[1].Bytes = 4;
    model_init(&m, v, 2);
    n = key(&t, &m, 0, NULL);
    TEXT_IS(&t, "  \"Huge\" = REG_BINARY, 70000 bytes: more than 65536, not "
                "read\n  \"After\" = REG_DWORD 1 (0x00000001)\n  2 values\n",
            "a value larger than the read");

    /* Grows between the size query and the read, once: the read after
     * shows what it found. */
    memset(v, 0, sizeof(v));
    v[0].Name = "Grows";
    v[0].Type = TR_REG_BINARY;
    v[0].Bytes = 60500UL;
    v[0].Sizes = growOnce;
    v[0].SizeCount = 5;
    model_init(&m, v, 1);
    n = key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  \"Grows\" = REG_BINARY, 60500 bytes, the first 256 shown:",
             "grown once, then read");
    TEXT_HAS(&t, "  1 value\n", "one value");

    /* At every read. */
    v[0].Sizes = growEvery;
    v[0].SizeCount = 7;
    model_init(&m, v, 1);
    n = key(&t, &m, 0, NULL);
    TEXT_IS(&t, "  \"Grows\" = REG_BINARY: its size changed at each of 3 "
                "reads; not shown\n  1 value\n", "grown at every read");

    /* Gone between the read and the size query. */
    memset(v, 0, sizeof(v));
    v[0].Name = "First";
    v[0].Type = TR_REG_DWORD;
    v[0].Data = dw;
    v[0].Bytes = 4;
    v[1].Name = "Gone";
    v[1].Type = TR_REG_BINARY;
    v[1].Bytes = 70000UL;
    model_init(&m, v, 2);
    m.GoneIndex = 1;
    n = key(&t, &m, 0, NULL);
    TEXT_IS(&t, "  \"First\" = REG_DWORD 1 (0x00000001)\n  1 value\n",
            "a value gone between the calls");

    /* 257 values: 256 listed, one counted. */
    for (i = 0; i < 257; i++) {
        many[i].Name = "V";
        many[i].Type = TR_REG_DWORD;
        many[i].Data = dw;
        many[i].Bytes = 4;
        many[i].Sizes = NULL;
    }
    model_init(&m, many, 257);
    n = key(&t, &m, 0, NULL);
    CHECK_EQ(n, 256, "256 values listed");
    TEXT_HAS(&t, "  (1 more value not listed: the report lists 256 values / "
                 "64 KB of a key)\n  256 values\n", "the 257th counted");
    model_init(&m, many, 256);
    n = key(&t, &m, 0, NULL);
    CHECK_EQ(n, 256, "256 of 256");
    CHECK(strstr(flat(&t), "more value") == NULL, "256 values: none left");

    /* The 64 KB bound: 400-byte values. */
    for (i = 0; i < 100; i++) {
        many[i].Name = "Bin";
        many[i].Type = TR_REG_BINARY;
        many[i].Data = pattern;
        many[i].Bytes = 400;
    }
    model_init(&m, many, 100);
    n = key(&t, &m, 0, NULL);
    CHECK(n > 0 && n < 100, "the text bound stops the listing");
    CHECK(t.Len <= TR_REG_KEY_TEXT + 256, "within 64 KB and its last lines");
    {
        char more[100];
        sprintf(more, "  (%d more values not listed", 100 - n);
        TEXT_HAS(&t, more, "the rest counted");
    }
    CHECK(!t.Overflow, "no overflow");
}

static void test_screen(void)
{
    static const unsigned char dw[4] = { 7, 0, 0, 0 };
    static const unsigned char utf16[] = {
        'H', 0, 'O', 0, 'S', 0, 'T', 0, 'A', 0, 0, 0
    };
    MV v[6];
    MODEL m;
    TR_TEXT t;

    memset(v, 0, sizeof(v));
    v[0].Name = "Owner";
    v[0].Type = TR_REG_SZ;
    v[0].Data = (const unsigned char *)"set up on HOSTA";
    v[0].Bytes = 16;
    v[1].Name = "Plain";
    v[1].Type = TR_REG_SZ;
    v[1].Data = (const unsigned char *)"fine";
    v[1].Bytes = 5;
    v[2].Name = "Wide";
    v[2].Type = TR_REG_BINARY;
    v[2].Data = utf16;
    v[2].Bytes = sizeof(utf16);
    v[3].Name = "HOSTA-made";
    v[3].Type = TR_REG_DWORD;
    v[3].Data = dw;
    v[3].Bytes = 4;
    v[4].Name = "Count";
    v[4].Type = TR_REG_DWORD;
    v[4].Data = dw;
    v[4].Bytes = 4;
    v[5].Name = "Odd name!";
    v[5].Type = TR_REG_SZ;
    v[5].Data = (const unsigned char *)"x";
    v[5].Bytes = 2;
    model_init(&m, v, 6);
    key(&t, &m, 0, &scrNames);
    TEXT_IS(&t, "  \"Owner\" = REG_SZ, 16 bytes: withheld (holds this "
                "machine's name)\n"
                "  \"Plain\" = REG_SZ \"fine\"\n"
                "  \"Wide\" = REG_BINARY, 12 bytes: withheld (holds this "
                "machine's name)\n"
                "  (a name holding this machine's name) = REG_DWORD 7 "
                "(0x00000007)\n"
                "  \"Count\" = REG_DWORD 7 (0x00000007)\n"
                "  \"Odd name!\" = REG_SZ \"x\"\n"
                "  6 values\n", "screened: names, strings, UTF-16");
    model_init(&m, v, 6);
    key(&t, &m, 0, &scrNone);
    TEXT_IS(&t, "  \"Owner\" = REG_SZ, 16 bytes: withheld (not screened)\n"
                "  \"Plain\" = REG_SZ, 5 bytes: withheld (not screened)\n"
                "  \"Wide\" = REG_BINARY, 12 bytes: withheld (not screened)\n"
                "  \"HOSTA-made\" = REG_DWORD 7 (0x00000007)\n"
                "  \"Count\" = REG_DWORD 7 (0x00000007)\n"
                "  \"Odd name?\" = REG_SZ, 2 bytes: withheld (not screened)\n"
                "  6 values\n", "not screened: numbers alone");
    /* Save Report's form: no screen. */
    model_init(&m, v, 6);
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  \"Owner\" = REG_SZ \"set up on HOSTA\"\n",
             "the report prints the data as read");
    TEXT_HAS(&t, "  \"Wide\" = REG_BINARY, 12 bytes: 48 00 4F 00 53 00 54 00 "
                 "41 00 00 00\n", "binary as read");
}

/* ---- a name read against a guard page (task 50.29's review: the word
 * test read the character after a word before the word's match had shown
 * the characters were there) ----------------------------------------------- */

/* Kernel32's page calls, declared here: <windows.h> does not compile under
 * the runner's /Za (as test_pipe.c declares them). */
#define TG_MEM_COMMIT     0x00001000UL
#define TG_MEM_RESERVE    0x00002000UL
#define TG_MEM_RELEASE    0x00008000UL
#define TG_PAGE_NOACCESS  0x01UL
#define TG_PAGE_READWRITE 0x04UL
__declspec(dllimport) void *__stdcall VirtualAlloc(void *address,
                                                   size_t size,
                                                   unsigned long type,
                                                   unsigned long protect);
__declspec(dllimport) int __stdcall VirtualFree(void *address, size_t size,
                                                unsigned long type);

#define TG_COMMIT   0x5000UL        /* room for 16383 characters and a NUL */

/* Three regions: an inaccessible page, TG_COMMIT bytes, an inaccessible
 * page. */
static char *guard;

/* name copied so that its NUL is the last byte before the trailing page. */
static const char *at_guard(const char *name)
{
    size_t n = strlen(name);
    char *p = guard + 0x1000UL + TG_COMMIT - (n + 1);

    memcpy(p, name, n + 1);
    return p;
}

/* name copied so that its first character is the first byte after the
 * leading page: a read one byte before the name faults. */
static const char *at_lead(const char *name)
{
    char *p = guard + 0x1000UL;

    memcpy(p, name, strlen(name) + 1);
    return p;
}

/* The rule's answer for name, read against each page; a fault fails the
 * suite at once. */
static void name_both(const char *name, int want, const char *what, int line)
{
    char buf[120];

    sprintf(buf, "%.60s: %s, after its NUL", what,
            want ? "withheld" : "shown");
    check_impl(tr_reg_secret_name(at_guard(name)) == want, buf, __FILE__,
               line);
    sprintf(buf, "%.60s: %s, before its start", what,
            want ? "withheld" : "shown");
    check_impl(tr_reg_secret_name(at_lead(name)) == want, buf, __FILE__,
               line);
}

#define NAME_BOTH(name, want, what) name_both((name), (want), (what), __LINE__)

static void test_name_bounds(void)
{
    static const char *const shortNo[] = {
        "", "K", "KE", "Ke", "P", "PI", "Pi", "xK", "aP", "xKE", "aPI",
        "Mon", "Turk", "HotKey", "HOTKEY", "KeyX", "S", "Ps", "Pw", "Ss",
        "Ssi", "Pas", "_", "1", "K_e_y", "K1E2Y", "Ke_y", "HotKeyS", "Hot",
        "HotKe", "Shortc", "HotKeyX", "Pin C", "Pin Cod", "Pin Codec",
        "WPAPs", "WPAPskX", "Hashes", "PinHashe", NULL
    };
    static const char *const shortYes[] = {
        "Key", "KEY", "Pin", "PIN", "x_PIN", "Pwd", "Psk", "Ssid", "Key0",
        "_Key", "Keys", "KeyS", "aPin", "Pins", "aKey", "KeyHex",
        "PinList", "KEYS", "XKEY", "HotKey1", "HotKey_", "Pin_Code",
        "Pin-Code", "Pin _Code", "WPAPsk", "PinHashes", "PinHashes_", NULL
    };
    static char longName[TR_REG_NAME_CHARS];
    static const unsigned char dw[4] = { 1, 0, 0, 0 };
    static MV v[2];
    MODEL m;
    TR_TEXT t;
    int i;

    guard = (char *)VirtualAlloc(NULL, TG_COMMIT + 0x2000UL, TG_MEM_RESERVE,
                                 TG_PAGE_NOACCESS);
    CHECK(guard != NULL, "the pages reserved");
    if (guard == NULL) {
        return;
    }
    CHECK(VirtualAlloc(guard + 0x1000UL, TG_COMMIT, TG_MEM_COMMIT,
                       TG_PAGE_READWRITE) == guard + 0x1000UL,
          "all but the two guard pages committed");
    for (i = 0; shortNo[i] != NULL; i++) {
        NAME_BOTH(shortNo[i], 0, shortNo[i]);
    }
    for (i = 0; shortYes[i] != NULL; i++) {
        NAME_BOTH(shortYes[i], 1, shortYes[i]);
    }
    for (i = 0; nameTable[i].Name != NULL; i++) {
        NAME_BOTH(nameTable[i].Name, nameTable[i].Withheld,
                  nameTable[i].Name);
    }
    /* The registry's longest name, 16383 characters, against each page. */
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    longName[TR_REG_NAME_CHARS - 1] = '\0';
    NAME_BOTH(longName, 0, "16383 characters");
    longName[TR_REG_NAME_CHARS - 2] = 'K';
    NAME_BOTH(longName, 0, "16383, ending K");
    longName[TR_REG_NAME_CHARS - 4] = 'K';
    longName[TR_REG_NAME_CHARS - 3] = 'e';
    longName[TR_REG_NAME_CHARS - 2] = 'y';
    NAME_BOTH(longName, 1, "16383, ending in the word Key");
    longName[TR_REG_NAME_CHARS - 3] = '_';
    NAME_BOTH(longName, 0, "16383, ending K_y");
    memset(longName, '_', TR_REG_NAME_CHARS - 4);
    longName[TR_REG_NAME_CHARS - 3] = 'e';
    NAME_BOTH(longName, 1, "16383, Key the one word at the end");
    longName[TR_REG_NAME_CHARS - 2] = 'S';
    NAME_BOTH(longName, 0, "16383, ending KeS");
    longName[TR_REG_NAME_CHARS - 4] = 'P';
    longName[TR_REG_NAME_CHARS - 3] = 'i';
    longName[TR_REG_NAME_CHARS - 2] = 'n';
    NAME_BOTH(longName, 1, "16383, ending Pin");
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    memcpy(longName + TR_REG_NAME_CHARS - 9, "Password", 8);
    NAME_BOTH(longName, 1, "16383, ending Password");
    memcpy(longName + TR_REG_NAME_CHARS - 9, "Pa_sswor", 8);
    NAME_BOTH(longName, 0,
              "16383, ending Pa_sswor: the term runs into the NUL");
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    memcpy(longName + TR_REG_NAME_CHARS - 8, "PinValu", 7);
    NAME_BOTH(longName, 0,
              "16383, ending PinValu: the form runs into the NUL");
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    memcpy(longName + TR_REG_NAME_CHARS - 10, "Passwords", 9);
    NAME_BOTH(longName, 1, "16383, ending Passwords");
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    memcpy(longName + TR_REG_NAME_CHARS - 15, "PasswordHashes", 14);
    NAME_BOTH(longName, 1, "16383, ending PasswordHashes");
    longName[TR_REG_NAME_CHARS - 2] = '\0';
    NAME_BOTH(longName, 0,
              "16382, ending PasswordHashe: the plural runs into the NUL");
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    longName[TR_REG_NAME_CHARS - 1] = '\0';
    memcpy(longName + TR_REG_NAME_CHARS - 9, "Pin Code", 8);
    NAME_BOTH(longName, 1, "16383, ending Pin Code");
    memcpy(longName + TR_REG_NAME_CHARS - 9, "aPin Cod", 8);
    NAME_BOTH(longName, 0,
              "16383, ending Pin Cod: CODE runs into the NUL");
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    memcpy(longName + TR_REG_NAME_CHARS - 7, "WPAPsk", 6);
    NAME_BOTH(longName, 1, "16383, ending WPAPsk");
    memset(longName, 'A', TR_REG_NAME_CHARS - 1);
    NAME_BOTH(longName, 0, "16383 capitals");
    memcpy(longName + TR_REG_NAME_CHARS - 4, "KEY", 3);
    NAME_BOTH(longName, 1, "16383 capitals, ending KEY");
    memset(longName, 'a', TR_REG_NAME_CHARS - 1);
    memcpy(longName, "Key", 3);
    NAME_BOTH(longName, 0, "16383, beginning Key");
    memcpy(longName, "HotKey", 6);
    NAME_BOTH(longName, 0, "16383, beginning HotKey");
    CHECK(tr_reg_hub_id(at_guard("USB\\ROOT_HUB")), "a root hub at the page");
    CHECK(!tr_reg_hub_id(at_guard("USB\\ROOT_HU")), "short of ROOT_HUB");
    CHECK(!tr_reg_hub_id(at_guard("USB\\XHCI98_HU")), "short of XHCI98_HUB");
    CHECK(!tr_reg_hub_id(at_guard("")), "an empty id");
    CHECK(tr_reg_hub_id(at_lead("USB\\ROOT_HUB")), "a root hub after a page");
    CHECK(!tr_reg_hub_id(at_lead("")), "an empty id after a page");
    VirtualFree(guard, 0, TG_MEM_RELEASE);
    guard = NULL;

    /* The longest name through a key: listed, cut at 128 with its length;
     * one longer than the buffer is said and skipped. */
    memset(longName, 'n', TR_REG_NAME_CHARS - 1);
    longName[TR_REG_NAME_CHARS - 1] = '\0';
    memset(v, 0, sizeof(v));
    v[0].Name = longName;
    v[0].Type = TR_REG_DWORD;
    v[0].Data = dw;
    v[0].Bytes = 4;
    model_init(&m, v, 1);
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "...\" (a name of 16383 characters) = REG_DWORD 1 "
                 "(0x00000001)\n  1 value\n", "a 16383-character name listed");
}

/* ---- a name handed back as characters and a length, with no NUL: the key
 * reading ends the name at its length before the rule reads it ---------- */

static void test_name_no_nul(void)
{
    static const unsigned char dw[4] = { 1, 0, 0, 0 };
    static MV v[3];
    MODEL m;
    TR_TEXT t;
    int i;

    memset(v, 0, sizeof(v));
    for (i = 0; i < 3; i++) {
        v[i].Type = TR_REG_DWORD;
        v[i].Data = dw;
        v[i].Bytes = 4;
    }
    v[0].Name = "Encryption";
    v[0].After = "Key";
    v[1].Name = "TrkKey";
    v[1].After = "board";
    v[2].Name = "Pin";
    v[2].After = "ned";
    model_init(&m, v, 3);
    key(&t, &m, 0, NULL);
    TEXT_IS(&t, "  \"Encryption\" = REG_DWORD 1 (0x00000001)\n"
                "  \"TrkKey\" = REG_DWORD, 4 bytes: withheld (its name "
                "suggests a key or password)\n"
                "  \"Pin\" = REG_DWORD, 4 bytes: withheld (its name suggests "
                "a key or password)\n"
                "  3 values\n",
            "names with no NUL, each ended at its length");
}

/* ---- the count past the limits (task 50.29's review: an error during the
 * count was read as its end) ---------------------------------------------- */

static void test_count(void)
{
    static const unsigned char dw[4] = { 1, 0, 0, 0 };
    static MV many[300];
    static char longName[TR_REG_NAME_CHARS + 8];
    MODEL m;
    TR_TEXT t;
    int n;
    int i;

    for (i = 0; i < 300; i++) {
        many[i].Name = "V";
        many[i].Type = TR_REG_DWORD;
        many[i].Data = dw;
        many[i].Bytes = 4;
        many[i].Sizes = NULL;
    }
    /* An error at value 260, four past the 256 listed. */
    model_init(&m, many, 300);
    m.ErrorIndex = 260;
    m.Error = 1450;
    n = key(&t, &m, 0, NULL);
    CHECK_EQ(n, 256, "256 listed before the error");
    /* Task 50.39: fitted to 79 columns, every word kept. */
    TEXT_HAS(&t, "  (4 or more values not listed: the report lists 256 values "
                 "/ 64 KB of a key;\n  the count stopped at value 260: error "
                 "1450)\n  256 values\n", "an error in the count said");
    /* An error at the first counted value. */
    model_init(&m, many, 300);
    m.ErrorIndex = 256;
    m.Error = 6;
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  (the values past the report's 256 values / 64 KB of a key "
                 "were not counted:\n  the count stopped at value 256: error "
                 "6)\n",
             "an error at once said");
    /* A name too long for the buffer still counts. */
    memset(longName, 'L', TR_REG_NAME_CHARS + 2);
    longName[TR_REG_NAME_CHARS + 2] = '\0';
    many[257].Name = longName;
    model_init(&m, many, 259);
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  (3 more values not listed: the report lists 256 values / "
                 "64 KB of a key)\n  256 values\n",
             "a long name counted, the count exact");
    many[257].Name = "V";
    /* The count's own bound. */
    model_init(&m, many, 256);
    m.Endless = 256UL + TR_REG_COUNT_MAX + 50UL;
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  (100000 or more values not listed: the report lists 256 "
                 "values / 64 KB of a\n  key, and counts no further than "
                 "100000)\n", "the count's bound said");
    model_init(&m, many, 256);
    m.Endless = 256UL + TR_REG_COUNT_MAX;
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  (100000 or more values not listed", "exactly the bound");
    model_init(&m, many, 256);
    m.Endless = 256UL + TR_REG_COUNT_MAX - 1UL;
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  (99999 more values not listed: the report lists 256 "
                 "values / 64 KB of a key)\n", "one short of the bound exact");
    /* Task 50.39: under a report node, the notes at the node's width. */
    keyWidth = 50;
    key(&t, &m, 0, NULL);
    keyWidth = 0;
    TEXT_HAS(&t, "  (99999 more values not listed:\n  the report lists 256 "
                 "values / 64 KB of a key)\n", "a note at the key's width");
}

/* ---- the read's own bound ------------------------------------------------- */

static void test_read_bound(void)
{
    static MV v[2];
    MODEL m;
    TR_TEXT t;

    memset(v, 0, sizeof(v));
    v[0].Name = "Exact";
    v[0].Type = TR_REG_BINARY;
    v[0].Bytes = TR_REG_READ_BYTES;
    v[1].Name = "OneMore";
    v[1].Type = TR_REG_BINARY;
    v[1].Bytes = TR_REG_READ_BYTES + 1UL;
    model_init(&m, v, 2);
    key(&t, &m, 0, NULL);
    TEXT_HAS(&t, "  \"Exact\" = REG_BINARY, 65536 bytes, the first 256 shown:\n"
                 "      0000  00 01 02 03", "65536 bytes read whole");
    TEXT_HAS(&t, "  \"OneMore\" = REG_BINARY, 65537 bytes: more than 65536, "
                 "not read\n  2 values\n", "65537 bytes not read");
    CHECK_EQ(m.Calls, 4, "65536 in one read; 65537 read, then sized");
}

/* ---- screening before any byte is replaced (task 50.29's review) --------- */

/* This machine's names in its code page, folded to capitals as the tool's
 * screen folds them: "HOSTA", the user "J\xD6RG" (Windows-1252, "Joerg"
 * with an o-umlaut) and "\xC6\xCE\xD0\xC0" (Windows-1251, a Cyrillic name).
 * The fold: ASCII's small letters, and 0xE0 to 0xFE but 0xF7 down by 0x20,
 * as both code pages fold them. */
static int fold(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
        return c - 0x20;
    }
    return c;
}

static int has_word(const char *text, const char *word)
{
    size_t n = strlen(word);
    const char *s;

    for (s = text; *s != '\0'; s++) {
        size_t k;
        for (k = 0; k < n; k++) {
            if (s[k] == '\0' ||
                fold((unsigned char)s[k]) != (unsigned char)word[k]) {
                break;
            }
        }
        if (k == n) {
            return 1;
        }
    }
    return 0;
}

static int screen_cp(const char *text, void *context)
{
    (void)context;
    return has_word(text, "HOSTA") || has_word(text, "J\xD6RG") ||
           has_word(text, "\xC6\xCE\xD0\xC0") ? TR_REG_WITHHOLD : TR_REG_SHOW;
}

/* UTF-16 into the code page as WideCharToMultiByte would for 1251 and
 * 1252 between them: Latin-1 as itself, Cyrillic A to ya at 0xC0. */
static size_t narrow_cp(const unsigned char *units, size_t count, char *out,
                        size_t outBytes, void *context)
{
    size_t i;

    (void)context;
    for (i = 0; i < count && i + 1 < outBytes; i++) {
        unsigned int u = units[2 * i] | ((unsigned int)units[2 * i + 1] << 8);
        if (u < 0x100) {
            out[i] = (char)u;
        } else if (u >= 0x410 && u <= 0x44F) {
            out[i] = (char)(0xC0 + (u - 0x410));
        } else {
            out[i] = '?';
        }
    }
    out[i] = '\0';
    return i;
}

static const TR_REG_SCR scrCp = { screen_cp, narrow_cp, NULL };

static void test_screen_first(void)
{
    /* "user J\xF6rg" in the code page. */
    static const unsigned char ansi[] = {
        0x01, 'u', 's', 'e', 'r', ' ', 'J', 0xF6, 'r', 'g', 0x00, 0x7F
    };
    /* Cyrillic "Zhora" in UTF-16: U+0416 U+043E U+0440 U+0430. */
    static const unsigned char wide[] = {
        0x16, 0x04, 0x3E, 0x04, 0x40, 0x04, 0x30, 0x04, 0x00, 0x00
    };
    static const unsigned char wideOdd[] = {
        0xAA, 0x16, 0x04, 0x3E, 0x04, 0x40, 0x04, 0x30, 0x04, 0x00
    };
    /* "J\xF6rg" in UTF-16, upper case. */
    static const unsigned char wideLatin[] = {
        'J', 0, 0xD6, 0, 'R', 0, 'G', 0
    };
    static const unsigned char plainBin[] = { 0x10, 0x20, 0x30, 0xC0, 0xFF };
    static MV v[8];
    MODEL m;
    TR_TEXT t;

    memset(v, 0, sizeof(v));
    v[0].Name = "Ansi";
    v[0].Type = TR_REG_BINARY;
    v[0].Data = ansi;
    v[0].Bytes = sizeof(ansi);
    v[1].Name = "AnsiText";
    v[1].Type = TR_REG_SZ;
    v[1].Data = (const unsigned char *)"Set up by j\xF6rg";
    v[1].Bytes = 16;
    v[2].Name = "Wide";
    v[2].Type = TR_REG_BINARY;
    v[2].Data = wide;
    v[2].Bytes = sizeof(wide);
    v[3].Name = "WideOdd";
    v[3].Type = TR_REG_BINARY;
    v[3].Data = wideOdd;
    v[3].Bytes = sizeof(wideOdd);
    v[4].Name = "WideLatin";
    v[4].Type = TR_REG_BINARY;
    v[4].Data = wideLatin;
    v[4].Bytes = sizeof(wideLatin);
    v[5].Name = "Last_J\xF6rg";
    v[5].Type = TR_REG_DWORD;
    v[5].Data = plainBin;
    v[5].Bytes = 4;
    v[6].Name = "Bytes";
    v[6].Type = TR_REG_BINARY;
    v[6].Data = plainBin;
    v[6].Bytes = sizeof(plainBin);
    v[7].Name = "Caf\xE9";
    v[7].Type = TR_REG_SZ;
    v[7].Data = (const unsigned char *)"na\xEFve \"quoted\"";
    v[7].Bytes = 15;
    model_init(&m, v, 8);
    key(&t, &m, 0, &scrCp);
    TEXT_IS(&t, "  \"Ansi\" = REG_BINARY, 12 bytes: withheld (holds this "
                "machine's name)\n"
                "  \"AnsiText\" = REG_SZ, 16 bytes: withheld (holds this "
                "machine's name)\n"
                "  \"Wide\" = REG_BINARY, 10 bytes: withheld (holds this "
                "machine's name)\n"
                "  \"WideOdd\" = REG_BINARY, 10 bytes: withheld (holds this "
                "machine's name)\n"
                "  \"WideLatin\" = REG_BINARY, 8 bytes: withheld (holds this "
                "machine's name)\n"
                "  (a name holding this machine's name) = REG_DWORD "
                "3224379408 (0xC0302010)\n"
                "  \"Bytes\" = REG_BINARY, 5 bytes: 10 20 30 C0 FF\n"
                "  \"Caf?\" = REG_SZ \"na?ve \"\"quoted\"\"\"\n"
                "  8 values\n",
            "the code page's names and UTF-16 screened first");
    /* A screen with no converter: the UTF-16 view is not taken, and the
     * bytes are shown as read (the converter is what finds that name). */
    model_init(&m, v, 8);
    key(&t, &m, 0, &scrNames);
    TEXT_HAS(&t, "  \"Wide\" = REG_BINARY, 10 bytes: 16 04 3E 04 40 04 30 04 "
                 "00 00\n", "no converter: the bytes as read");
}

/* ---- the screen's windows, as the device report's screen uses them ------- */

static int windowCalls;
static int windowTooLong;

static int has_hosta(const char *chunk, void *context)
{
    (void)context;
    windowCalls++;
    if (strlen(chunk) > TR_REG_WINDOW) {
        windowTooLong++;
    }
    return strstr(chunk, "HOSTA") != NULL;
}

static char winText[TR_REG_READ_BYTES + 8];
static char winChunk[TR_REG_WINDOW + 1];

/* A text of n characters with HOSTA at `at` (or none, at < 0): found, and
 * the windows asked. */
static int windows_find(size_t n, long at, int *calls)
{
    int found;

    memset(winText, 'x', n);
    winText[n] = '\0';
    if (at >= 0) {
        memcpy(winText + at, "HOSTA", 5);
    }
    windowCalls = 0;
    found = tr_reg_screen_windows(winText, winChunk, has_hosta, NULL);
    if (calls != NULL) {
        *calls = windowCalls;
    }
    return found;
}

/* The device report's screen in shape (tr_snap.c's keys_screen): the
 * windows over the text. */
static int screen_windows(const char *text, void *context)
{
    (void)context;
    return tr_reg_screen_windows(text, winChunk, has_hosta, NULL)
               ? TR_REG_WITHHOLD
               : TR_REG_SHOW;
}

static const TR_REG_SCR scrWindows = { screen_windows, NULL, NULL };

static void test_windows(void)
{
    static unsigned char big[60000];
    static MV v[2];
    MODEL m;
    TR_TEXT t;
    int calls;

    windowTooLong = 0;
    CHECK(!windows_find(100, -1, &calls) && calls == 1, "a short text: one");
    CHECK(windows_find(100, 0, NULL), "a name at the start");
    CHECK(windows_find(1800, 1795, &calls) && calls == 1,
          "1800 characters: one window, the name at its end");
    CHECK(!windows_find(1800, -1, &calls) && calls == 1, "1800: one window");
    CHECK(!windows_find(1801, -1, &calls) && calls == 2, "1801: two windows");
    CHECK(windows_find(1801, 1796, &calls) && calls == 2,
          "a name over the first window's edge, found in the second");
    CHECK(windows_find(4000, 1797, NULL), "1797: over the edge");
    CHECK(windows_find(4000, 1499, NULL), "1499");
    CHECK(windows_find(4000, 3297, NULL), "3297: over the second edge");
    CHECK(!windows_find(3300, -1, &calls) && calls == 2, "3300: two windows");
    CHECK(!windows_find(3301, -1, &calls) && calls == 3, "3301: three");
    CHECK(windows_find(TR_REG_READ_BYTES, (long)TR_REG_READ_BYTES - 5, NULL),
          "65536 characters, the name at the end");
    CHECK(windows_find(TR_REG_READ_BYTES, 40000, NULL), "... at 40000");
    CHECK(!windows_find(TR_REG_READ_BYTES, -1, NULL), "... none");
    CHECK(!windows_find(0, -1, &calls) && calls == 1, "an empty text");
    CHECK_EQ(windowTooLong, 0, "no window longer than 1800 characters");

    /* Through a key: a 60000-byte string with the name at 59000, and one
     * over a window's edge. */
    memset(big, 'y', sizeof(big));
    memcpy(big + 59000, "HOSTA", 5);
    big[sizeof(big) - 1] = 0;
    memset(v, 0, sizeof(v));
    v[0].Name = "Long";
    v[0].Type = TR_REG_SZ;
    v[0].Data = big;
    v[0].Bytes = sizeof(big);
    model_init(&m, v, 1);
    key(&t, &m, 0, &scrWindows);
    TEXT_IS(&t, "  \"Long\" = REG_SZ, 60000 bytes: withheld (holds this "
                "machine's name)\n  1 value\n", "a name deep in a long string");
    memset(big, 'y', sizeof(big));
    memcpy(big + 1798, "HOSTA", 5);
    big[sizeof(big) - 1] = 0;
    model_init(&m, v, 1);
    key(&t, &m, 0, &scrWindows);
    TEXT_HAS(&t, "withheld (holds this machine's name)", "over an edge");
    memset(big, 'y', sizeof(big));
    big[sizeof(big) - 1] = 0;
    model_init(&m, v, 1);
    key(&t, &m, 0, &scrWindows);
    TEXT_HAS(&t, "  \"Long\" = REG_SZ \"yyyy", "no name: shown");
}

/* ---- the walk over one device's devnodes --------------------------------- */

#define FT_MAX      64

typedef struct _FNODE {
    int Parent;
    const char *Id;
    int Hub;
    TR_U32 ChildError;      /* answered for its first child, or 0 */
    TR_U32 SiblingError;    /* answered after its last child, or 0 */
} FNODE;

typedef struct _FTREE {
    FNODE N[FT_MAX];
    int Count;
    char Log[8192];
    int StopAt;             /* the Emit call that fails, or 0 */
    int Emits;
    int Keys;               /* list each devnode's modelled key */
    TR_TEXT *Text;
} FTREE;

static FTREE ft;

static void ft_init(void)
{
    memset(&ft, 0, sizeof(ft));
}

static int ft_add(int parent, const char *id)
{
    ft.N[ft.Count].Parent = parent;
    ft.N[ft.Count].Id = id;
    return ft.Count++;
}

static void ft_log(const char *s)
{
    if (strlen(ft.Log) + strlen(s) + 1 < sizeof(ft.Log)) {
        strcat(ft.Log, s);
    }
}

static TR_U32 ft_child(void *context, TR_U32 node, TR_U32 *child)
{
    int j;

    (void)context;
    if (ft.N[node].ChildError != 0) {
        return ft.N[node].ChildError;
    }
    for (j = 0; j < ft.Count; j++) {
        if (ft.N[j].Parent == (int)node) {
            *child = (TR_U32)j;
            return TR_REG_REL_OK;
        }
    }
    return TR_REG_REL_NONE;
}

static TR_U32 ft_sibling(void *context, TR_U32 node, TR_U32 *next)
{
    int parent = ft.N[node].Parent;
    int j;

    (void)context;
    for (j = (int)node + 1; j < ft.Count; j++) {
        if (ft.N[j].Parent == parent) {
            *next = (TR_U32)j;
            return TR_REG_REL_OK;
        }
    }
    return parent >= 0 && ft.N[parent].SiblingError != 0
               ? ft.N[parent].SiblingError
               : TR_REG_REL_NONE;
}

static int ft_hub(void *context, TR_U32 node)
{
    (void)context;
    return ft.N[node].Hub || tr_reg_hub_id(ft.N[node].Id);
}

/* Each devnode's key: one value naming the devnode. */
static const char *ftKeyName;

static TR_U32 ft_key_enum(void *context, TR_U32 index, char *name,
                          TR_U32 *nameChars, TR_U32 *type,
                          unsigned char *data, TR_U32 *bytes)
{
    size_t n = strlen(ftKeyName);

    (void)context;
    if (index != 0) {
        return TR_REG_NO_MORE;
    }
    strcpy(name, "Diag");
    *nameChars = 4;
    *type = TR_REG_SZ;
    if (data != NULL) {
        memcpy(data, ftKeyName, n + 1);
    }
    *bytes = (TR_U32)n + 1;
    return TR_REG_OK;
}

static int ft_emit(void *context, TR_U32 node, int level)
{
    char line[200];

    (void)context;
    ft.Emits++;
    if (ft.StopAt != 0 && ft.Emits == ft.StopAt) {
        return 0;
    }
    sprintf(line, "%d:%s@%d\n", level, ft.N[node].Id, (int)node);
    ft_log(line);
    if (ft.Keys) {
        TR_REG_KEY k;

        memset(&k, 0, sizeof(k));
        k.Enum = ft_key_enum;
        k.Data = dataBuf;
        k.Name = nameBuf;
        k.Proj = projBuf;
        ftKeyName = ft.N[node].Id;
        tr_printf(ft.Text, "[%d] %s\n", level, ft.N[node].Id);
        tr_reg_key_text(ft.Text, "  ", &k);
    }
    return 1;
}

static int ft_note(void *context, int level, const char *line)
{
    char s[300];

    (void)context;
    sprintf(s, "N%d:%s\n", level, line);
    ft_log(s);
    if (ft.Keys) {
        tr_printf(ft.Text, "%s\n", line);
    }
    return ft.StopAt == -1 ? 0 : 1;
}

static int ft_walk(int device)
{
    TR_REG_TREE w;

    w.Child = ft_child;
    w.Sibling = ft_sibling;
    w.IsHub = ft_hub;
    w.Emit = ft_emit;
    w.Note = ft_note;
    w.Context = NULL;
    return tr_reg_tree_walk(&w, (TR_U32)device);
}

static void check_log(const char *want, const char *what, int line)
{
    int ok = strcmp(ft.Log, want) == 0;

    check_impl(ok, what, __FILE__, line);
    if (!ok) {
        printf("---- got:\n%s---- want:\n%s----\n", ft.Log, want);
    }
}

#define LOG_IS(want, what) check_log((want), (what), __LINE__)

static void test_tree(void)
{
    static char ids[40][32];
    TR_TEXT t;
    int root;
    int dev;
    int a;
    int b;
    int c;
    int d;
    int e;
    int n;
    int i;

    /* A composite device: an audio function; a function that is itself
     * composite, whose nested function carries HID devnodes three and four
     * levels down, and one five down; a HID function with two collections.
     * Beside it in the tree, a hub with a device on its port, and the root
     * hub above them. */
    ft_init();
    root = ft_add(-1, "USB\\ROOT_HUB30\\4&1");
    dev = ft_add(root, "USB\\VID_1234&PID_0001\\5&1");
    ft_add(dev, "USB\\VID_1234&PID_0001&MI_00\\6&1");
    a = ft_add(dev, "USB\\VID_1234&PID_0001&MI_01\\6&2");
    b = ft_add(a, "USB\\VID_1234&PID_0001&MI_01&MI_00\\7&1");
    c = ft_add(b, "HID\\VID_1234&PID_0001&MI_01\\8&1");
    d = ft_add(c, "HID\\KBD_CHILD\\9&1");
    e = ft_add(d, "XYZ\\TOO_DEEP\\10&1");
    ft_add(e, "XYZ\\DEEPER\\11&1");
    a = ft_add(dev, "USB\\VID_1234&PID_0001&MI_02\\6&3");
    ft_add(a, "HID\\VID_1234&PID_0001&MI_02&COL01\\7&2");
    ft_add(a, "HID\\VID_1234&PID_0001&MI_02&COL02\\7&3");
    a = ft_add(root, "USB\\VID_0409&PID_005A\\5&2");
    ft.N[a].Hub = 1;
    ft_add(a, "USB\\VID_0627&PID_0001\\6&9");
    n = ft_walk(dev);
    CHECK_EQ(n, 9, "nine devnodes of the composite device");
    LOG_IS("0:USB\\VID_1234&PID_0001\\5&1@1\n"
           "1:USB\\VID_1234&PID_0001&MI_00\\6&1@2\n"
           "1:USB\\VID_1234&PID_0001&MI_01\\6&2@3\n"
           "2:USB\\VID_1234&PID_0001&MI_01&MI_00\\7&1@4\n"
           "3:HID\\VID_1234&PID_0001&MI_01\\8&1@5\n"
           "4:HID\\KBD_CHILD\\9&1@6\n"
           "N5:(the devnodes below it are not listed: the report follows "
           "devnodes 4 levels below a USB device)\n"
           "1:USB\\VID_1234&PID_0001&MI_02\\6&3@9\n"
           "2:HID\\VID_1234&PID_0001&MI_02&COL01\\7&2@10\n"
           "2:HID\\VID_1234&PID_0001&MI_02&COL02\\7&3@11\n",
           "nested functions and HID devnodes, to four levels");

    /* The hub: itself, not the device on its port. */
    ft.Log[0] = '\0';
    n = ft_walk(12);
    CHECK_EQ(n, 1, "a hub alone");
    LOG_IS("0:USB\\VID_0409&PID_005A\\5&2@12\n", "a hub's ports not followed");
    /* xhci98's hub by its id alone, below a device. */
    ft_init();
    dev = ft_add(-1, "USB\\VID_1&PID_2\\1");
    a = ft_add(dev, "USB\\XHCI98_HUB\\2");
    ft_add(a, "USB\\VID_3&PID_4\\3");
    n = ft_walk(dev);
    LOG_IS("0:USB\\VID_1&PID_2\\1@0\n1:USB\\XHCI98_HUB\\2@1\n",
           "xhci98's hub by id: not followed");
    CHECK_EQ(n, 2, "two");

    /* The devnode limit: 40 functions, 32 devnodes listed, 9 counted. */
    ft_init();
    dev = ft_add(-1, "USB\\VID_1&PID_2\\1");
    for (i = 0; i < 40; i++) {
        sprintf(ids[i], "USB\\VID_1&PID_2&MI_%02d\\%d", i, i);
        ft_add(dev, ids[i]);
    }
    n = ft_walk(dev);
    CHECK_EQ(n, 32, "32 devnodes listed");
    CHECK(strstr(ft.Log, "1:USB\\VID_1&PID_2&MI_30\\30@31\n") != NULL &&
          strstr(ft.Log, "MI_31") == NULL, "the 32nd is MI_30");
    CHECK(strstr(ft.Log, "N1:(9 more devnodes below this device not listed: "
                         "the report lists 32 devnodes of one device)\n") !=
              NULL, "the rest counted");

    /* Devnodes not all read: an error for the first child, and the walk's
     * own "not all read" after the last. */
    ft_init();
    dev = ft_add(-1, "USB\\VID_1&PID_2\\1");
    a = ft_add(dev, "USB\\VID_1&PID_2&MI_00\\2");
    ft.N[a].ChildError = 13;
    b = ft_add(dev, "USB\\VID_1&PID_2&MI_01\\3");
    ft_add(b, "HID\\X\\4");
    ft.N[b].SiblingError = TR_REG_REL_PARTIAL;
    n = ft_walk(dev);
    LOG_IS("0:USB\\VID_1&PID_2\\1@0\n"
           "1:USB\\VID_1&PID_2&MI_00\\2@1\n"
           "N2:(the devnodes below it were not all read: error 13)\n"
           "1:USB\\VID_1&PID_2&MI_01\\3@2\n"
           "2:HID\\X\\4@3\n"
           "N2:(the devnodes below it were not all read)\n",
           "devnodes not all read, said");
    CHECK_EQ(n, 4, "four listed");

    /* A device with no devnodes below it. */
    ft_init();
    dev = ft_add(-1, "USB\\VID_1&PID_2\\1");
    CHECK_EQ(ft_walk(dev), 1, "the device alone");

    /* An output that fails stops the walk. */
    ft_init();
    dev = ft_add(-1, "USB\\VID_1&PID_2\\1");
    ft_add(dev, "USB\\VID_1&PID_2&MI_00\\2");
    ft_add(dev, "USB\\VID_1&PID_2&MI_01\\3");
    ft.StopAt = 2;
    CHECK_EQ(ft_walk(dev), -1, "an Emit that failed");
    LOG_IS("0:USB\\VID_1&PID_2\\1@0\n", "nothing after it");
    ft_init();
    dev = ft_add(-1, "USB\\VID_1&PID_2\\1");
    a = ft_add(dev, "USB\\VID_1&PID_2&MI_00\\2");
    ft.N[a].ChildError = 5;
    ft.StopAt = -1;
    CHECK_EQ(ft_walk(dev), -1, "a Note that failed");

    /* Every devnode's key through the walk: the nested function's and the
     * HID devnodes' keys are in the one text. */
    ft_init();
    dev = ft_add(-1, "USB\\VID_1234&PID_0001\\5&1");
    a = ft_add(dev, "USB\\VID_1234&PID_0001&MI_01\\6&2");
    b = ft_add(a, "USB\\VID_1234&PID_0001&MI_01&MI_00\\7&1");
    ft_add(b, "HID\\VID_1234&PID_0001&MI_01\\8&1");
    a = ft_add(dev, "USB\\VID_1234&PID_0001&MI_02\\6&3");
    ft_add(a, "HID\\VID_1234&PID_0001&MI_02&COL01\\7&2");
    ft.Keys = 1;
    tr_text_init(&t, textBuf, sizeof(textBuf));
    ft.Text = &t;
    CHECK_EQ(ft_walk(dev), 6, "six devnodes with their keys");
    TEXT_IS(&t, "[0] USB\\VID_1234&PID_0001\\5&1\n"
                "  \"Diag\" = REG_SZ \"USB\\VID_1234&PID_0001\\5&1\"\n"
                "  1 value\n"
                "[1] USB\\VID_1234&PID_0001&MI_01\\6&2\n"
                "  \"Diag\" = REG_SZ \"USB\\VID_1234&PID_0001&MI_01\\6&2\"\n"
                "  1 value\n"
                "[2] USB\\VID_1234&PID_0001&MI_01&MI_00\\7&1\n"
                "  \"Diag\" = REG_SZ \"USB\\VID_1234&PID_0001&MI_01&MI_00"
                "\\7&1\"\n"
                "  1 value\n"
                "[3] HID\\VID_1234&PID_0001&MI_01\\8&1\n"
                "  \"Diag\" = REG_SZ \"HID\\VID_1234&PID_0001&MI_01\\8&1\"\n"
                "  1 value\n"
                "[1] USB\\VID_1234&PID_0001&MI_02\\6&3\n"
                "  \"Diag\" = REG_SZ \"USB\\VID_1234&PID_0001&MI_02\\6&3\"\n"
                "  1 value\n"
                "[2] HID\\VID_1234&PID_0001&MI_02&COL01\\7&2\n"
                "  \"Diag\" = REG_SZ \"HID\\VID_1234&PID_0001&MI_02&COL01"
                "\\7&2\"\n"
                "  1 value\n", "every devnode's key in one text");

    CHECK(tr_reg_hub_id("USB\\ROOT_HUB30\\4&1"), "ROOT_HUB30");
    CHECK(tr_reg_hub_id("usb\\xhci98_hub30\\x"), "xhci98's SuperSpeed hub");
    CHECK(!tr_reg_hub_id("USB\\VID_1&PID_2\\HUBBY"), "not a hub");
    CHECK(!tr_reg_hub_id(NULL), "NULL");
}

int main(void)
{
    size_t i;

    for (i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (unsigned char)(i & 0xFF);
    }
    test_names();
    test_types();
    test_strings();
    test_multi();
    test_hex_limit();
    test_key();
    test_screen();
    test_name_bounds();
    test_name_no_nul();
    test_count();
    test_read_bound();
    test_screen_first();
    test_windows();
    test_tree();
    printf("test_trreg: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
