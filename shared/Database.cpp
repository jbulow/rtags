#include "Database.h"
#include <dirent.h>

Database::Database()
    : mMode(ReadOnly), mRefIdxCounter(0)
{}

Database::~Database()
{
}

static inline int removeDirectory(const char *path)
{
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = -1;

    if (d) {
        struct dirent *p;

        r = 0;

        while (!r && (p=readdir(d))) {
            int r2 = -1;
            char *buf;
            size_t len;

            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
                continue;
            }

            len = path_len + strlen(p->d_name) + 2;
            buf = static_cast<char*>(malloc(len));

            if (buf) {
                struct stat statbuf;
                snprintf(buf, len, "%s/%s", path, p->d_name);
                if (!stat(buf, &statbuf)) {
                    if (S_ISDIR(statbuf.st_mode)) {
                        r2 = removeDirectory(buf);
                    } else {
                        r2 = unlink(buf);
                    }
                }

                free(buf);
            }

            r = r2;
        }

        closedir(d);
    }

    if (!r) {
        r = rmdir(path);
    }

    return r;
}


bool Database::open(const Path &db, Mode mode)
{
    Q_ASSERT(!isOpened());
    if (mode == WriteOnly)
        removeDirectory(db);
    if (openDatabase(db, mode)) {
        mMode = mode;
        for (int i=0; i<NumConnections; ++i) {
            mConnections[i] = createConnection(static_cast<ConnectionType>(i));
            Q_ASSERT(mConnections[i]);
        }

        mFilesByName = read<QHash<Path, unsigned> >("filesByName");
        for (QHash<Path, unsigned>::const_iterator it = mFilesByName.begin();
             it != mFilesByName.end(); ++it) {
            mFilesByIndex[it.value()] = it.key();
        }
        return true;
    }
    return false;
}

void Database::close()
{
    switch (mMode) {
    case ReadWrite:
        for (QHash<QByteArray, QSet<Location> >::const_iterator it = mDictionary.begin(); it != mDictionary.end(); ++it) {
            QSet<Location> l = read<QSet<Location> >(Dictionary, it.key());
            l += it.value();
            write(Dictionary, it.key(), l);
        }
        break;
    case WriteOnly:
        for (QHash<QByteArray, QSet<Location> >::const_iterator it = mDictionary.begin(); it != mDictionary.end(); ++it)
            write(Dictionary, it.key(), it.value());
        break;
    case ReadOnly:
        Q_ASSERT(0);
        break;
    }
    closeDatabase();
}


Location Database::followLocation(const Location &source) const
{
    Location ret;
    if (source.file) {
        char buf[32];
        const int written = snprintf(buf, 32, "%d:%d:%d", source.file, source.line, source.column);
        Q_ASSERT(written < 32);
        ret = read<Location>(Targets, QByteArray::fromRawData(buf, written));
    }
    return ret;
}

QSet<Location> Database::findReferences(const Location &source) const
{
    QSet<Location> ret;
    if (source.file) {
        char buf[32];
        int written = snprintf(buf, 32, "%d:%d:%d", source.file, source.line, source.column);
        Q_ASSERT(written < 32);

        const int refId = read<int>(References, QByteArray::fromRawData(buf, written));
        if (refId > 0) {
            written = snprintf(buf, 32, "%d", refId);
            Q_ASSERT(written < 32);
            ret = read<QSet<Location> >(References, QByteArray::fromRawData(buf, written));
        }
    }
    return ret;
}

QSet<Location> Database::findSymbol(const QByteArray &symbolName) const
{
    return read<QSet<Location> >(char('a' + Dictionary) + symbolName);
}

QList<QByteArray> Database::symbolNames(const QByteArray &filter) const
{
    QList<QByteArray> ret;
    iterator *it = createIterator();
    Q_ASSERT(it);
    const char ch = ('a' + Dictionary);
    const QByteArray pref = QByteArray(&ch, 1);
    if (it->seek(pref)) {
        do {
            const QByteArray key = it->key();
            if (key.size() < 2 || strncmp(key.constData(), pref, 1)) {
                break;
            }
            if (filter.isEmpty() || it->key().contains(filter))
                ret.append(key.mid(2));
        } while (it->next());
    }
    return ret;
}

Location Database::createLocation(const QByteArray &arg, const Path &cwd)
{
    assert(!arg.empty());
    int colon = arg.lastIndexOf(':');
    if (colon == arg.size() - 1)
        colon = arg.lastIndexOf(':', colon - 1);
    if (colon == -1) {
        return Location();
    }
    const unsigned col = atoi(arg.constData() + colon + 1);
    if (!col) {
        return Location();
    }
    colon = arg.lastIndexOf(':', colon - 1);
    if (colon == -1) {
        return Location();
    }
    const unsigned line = atoi(arg.constData() + colon + 1);
    if (!line) {
        return Location();
    }
    Path file = Path::resolved(arg.left(colon), cwd);
    const int fileId = mFilesByName.value(file, 0);
    if (!fileId) {
        return Location();
    }
    Location loc;
    loc.file = fileId;
    loc.line = line;
    loc.column = col;
    return loc;
}


static inline void addLocations(const Location &definition, const QSet<Location> &declarations,
                                const QByteArray &key, QHash<QByteArray, QSet<Location> >& dict)
{
    QSet<Location> &locations = dict[key];
    if (definition.file)
        locations.insert(definition);
    foreach(const Location &declaration, declarations)
        locations.insert(declaration);
}

static inline void collectDict(QByteArray name, const QList<QByteArray> &parentNames,
                               QHash<QByteArray, QSet<Location> >& dict,
                               const Location &definition,
                               const QSet<Location> &declarations)
{
    int colon = name.indexOf('('); // the name we have doesn't include args which kind sorta sucks a little
    if (colon != -1)
        addLocations(definition, declarations, QByteArray::fromRawData(name.constData(), colon), dict);
    addLocations(definition, declarations, name, dict);
    foreach(const QByteArray &cur, parentNames) {
        const int old = name.size();
        name.prepend("::");
        name.prepend(cur);
        if (colon != -1) {
            colon += (name.size() - old);
            addLocations(definition, declarations, QByteArray::fromRawData(name.constData(), colon), dict);
        }
        addLocations(definition, declarations, name, dict);
    }
}

void Database::writeEntity(const QByteArray &symbolName,
                           const QList<QByteArray> &parentNames,
                           const Location &definition,
                           const QSet<Location> &declarations,
                           const QSet<Location> &references)
{
    Q_ASSERT(definition.file || !declarations.isEmpty());
    char buf[512];
    int refIdx = 0;
    if (!references.isEmpty()) {
        refIdx = ++mRefIdxCounter;
        const int ret = snprintf(buf, 512, "%d", refIdx);
        write(References, QByteArray::fromRawData(buf, ret), references);
        const Location loc = (definition.file ? definition : *declarations.begin());
        for (QSet<Location>::const_iterator it = references.begin();
             it != references.end(); ++it) {
            const Location &l = *it;
            const int ret = snprintf(buf, 512, "%d:%d:%d", l.file, l.line, l.column);
            write(Targets, QByteArray::fromRawData(buf, ret), loc);
        }
        if (definition.file || refIdx) {
            const int ret = snprintf(buf, 512, "%d:%d:%d", definition.file, definition.line, definition.column);
            if (declarations.size() == 1) {
                write(Targets, QByteArray::fromRawData(buf, ret), *declarations.begin());
            }
            write(References, QByteArray::fromRawData(buf, ret), refIdx);

            foreach(const Location &declaration, declarations) {
                const int ret = snprintf(buf, 512, "%d:%d:%d", declaration.file, declaration.line, declaration.column);
                if (definition.file) {
                    write(Targets, QByteArray::fromRawData(buf, ret), definition);
                }
                if (refIdx)
                    write(References, QByteArray::fromRawData(buf, ret), refIdx);
            }
        }
    }
    collectDict(symbolName, parentNames, mDictionary, definition, declarations);
}

QByteArray Database::locationToString(const Location &location) const
{
    if (location.file) {
        QByteArray ret = mFilesByIndex.value(location.file);
        char buf[32];
        snprintf(buf, 32, ":%d:%d:", location.line, location.column);
        ret += buf;
        return ret;
    }
    return QByteArray();
}