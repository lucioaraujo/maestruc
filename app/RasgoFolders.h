// Cópia de RASGO/REFERENCIAS/PASTAS/src/RasgoFolders.h (folders-ref 1.0, 18/11/2026).
// RASGO — pastas e dados do usuário, versão de referência comum.
// folders-ref 1.0 (18/11/2026) · Lúcio de Araújo · AGPL-3.0-or-later
//
// Implementa RASGO/RASGO_DOCUMENTATION/CONVENCAO_PASTAS_E_DADOS.md, método da
// família desde 18/11/2026. Cada instrumento COPIA este arquivo (não é
// dependência) com a linha de proveniência, como a saida-ref.
//
//   dados do app   <data>/RASGO/<Instrument>/      preferências, memórias,
//                                                  patches, sessão, logs, cache
//   material       <Music>/RASGO/<Instrument>/     gravações, álbuns, sessões
//                                                  salvas, fontes
//
//   <data>   Linux $XDG_CONFIG_HOME ou ~/.config · Windows %APPDATA% ·
//            macOS ~/Library/Application Support
//   <Music>  a pasta de música do sistema (userMusicDirectory)
//
// Nomes: pasta do instrumento com as palavras juntas (RasgoModular);
// diretórios e arquivos em inglês, sem espaços, acentos nem caracteres
// especiais. RASGO_DATA_DIR e RASGO_MATERIAL_DIR mudam as duas raízes.
//
// Só juce_core e juce_data_structures. Nada aqui apaga ou move: a migração COPIA, uma vez só.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>   // PropertiesFile

#include <memory>

namespace rasgo::folders
{

// Variável de ambiente com um caminho absoluto, ou File{} se não houver.
inline juce::File environmentDirectory (const char* name)
{
    const auto value = juce::SystemStats::getEnvironmentVariable (name, {}).trim();
    if (value.isEmpty() || ! juce::File::isAbsolutePath (value))
        return {};
    return juce::File (value);
}

// <data>/RASGO
inline juce::File dataRoot()
{
    if (const auto e = environmentDirectory ("RASGO_DATA_DIR"); e != juce::File{})
        return e;
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support").getChildFile ("RASGO");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("RASGO");
   #else
    // o JUCE lê o XDG do user-dirs.dirs, não do ambiente: lido aqui
    const auto xdg = environmentDirectory ("XDG_CONFIG_HOME");
    return (xdg != juce::File{} ? xdg : juce::File ("~/.config")).getChildFile ("RASGO");
   #endif
}

// <Music>/RASGO
inline juce::File materialRoot()
{
    if (const auto e = environmentDirectory ("RASGO_MATERIAL_DIR"); e != juce::File{})
        return e;
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("RASGO");
}

// instrument: o nome da pasta, palavras juntas ("RasgoModular", "Navalha2").
inline juce::File appData (const juce::String& instrument)  { return dataRoot().getChildFile (instrument); }
inline juce::File material (const juce::String& instrument) { return materialRoot().getChildFile (instrument); }

// <Music>/RASGO/<Instrument>/recordings/YYYY-MM-DD (não cria)
inline juce::File recordingsOfDay (const juce::String& instrument,
                                   juce::Time when = juce::Time::getCurrentTime())
{
    return material (instrument).getChildFile ("recordings").getChildFile (when.formatted ("%Y-%m-%d"));
}

// Nome seguro para pasta ou arquivo a partir de um texto do usuário:
// minúsculas, acentos fora, todo o resto vira hífen, sem hífens repetidos
// nem nas pontas. "Álbum de Inverno" -> "album-de-inverno". O texto
// original deve ser guardado ao lado (.json/.txt), não no nome.
inline juce::String safeName (const juce::String& text, const juce::String& fallback = "untitled")
{
    static const juce::String from = juce::String (juce::CharPointer_UTF8 (
        "\xc3\xa0\xc3\xa1\xc3\xa2\xc3\xa3\xc3\xa4\xc3\xa5"    // àáâãäå
        "\xc3\xa8\xc3\xa9\xc3\xaa\xc3\xab"                    // èéêë
        "\xc3\xac\xc3\xad\xc3\xae\xc3\xaf"                    // ìíîï
        "\xc3\xb2\xc3\xb3\xc3\xb4\xc3\xb5\xc3\xb6\xc3\xb8"    // òóôõöø
        "\xc3\xb9\xc3\xba\xc3\xbb\xc3\xbc"                    // ùúûü
        "\xc3\xa7\xc3\xb1\xc3\xbd\xc3\xbf"                    // çñýÿ
        // maiúsculas: o toLowerCase depende do locale e pode não baixá-las
        "\xc3\x80\xc3\x81\xc3\x82\xc3\x83\xc3\x84\xc3\x85"    // ÀÁÂÃÄÅ
        "\xc3\x88\xc3\x89\xc3\x8a\xc3\x8b"                    // ÈÉÊË
        "\xc3\x8c\xc3\x8d\xc3\x8e\xc3\x8f"                    // ÌÍÎÏ
        "\xc3\x92\xc3\x93\xc3\x94\xc3\x95\xc3\x96\xc3\x98"    // ÒÓÔÕÖØ
        "\xc3\x99\xc3\x9a\xc3\x9b\xc3\x9c"                    // ÙÚÛÜ
        "\xc3\x87\xc3\x91\xc3\x9d"));                         // ÇÑÝ
    static const juce::String to = "aaaaaaeeeeiiiioooooouuuucnyy"
                                   "aaaaaaeeeeiiiioooooouuuucny";

    juce::String out;
    bool dash = false;
    for (auto c : text)
    {
        if (const int i = from.indexOfChar (c); i >= 0)
            c = to[i];
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
        juce::String piece;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))  piece = juce::String::charToString (c);
        else if (c == 0xdf)                                     piece = "ss";   // ß
        else if (c == 0xe6 || c == 0xc6)                        piece = "ae";   // æ Æ
        else if (c == 0x153 || c == 0x152)                      piece = "oe";   // œ Œ
        if (piece.isEmpty()) { dash = out.isNotEmpty(); continue; }
        if (dash) out << '-';
        out << piece;
        dash = false;
    }
    return out.isEmpty() ? fallback : out;
}

// Registro do que uma migração copiou, na pasta de destino.
inline void noteMigration (const juce::File& directory, const juce::File& source)
{
    directory.createDirectory();
    directory.getChildFile ("migrated-from.txt").appendText (
        juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  " + source.getFullPathName() + "\n");
}

// Copia um arquivo antigo para o lugar novo, uma vez só: só se o novo ainda
// não existe e o antigo existe. O antigo fica intacto. true se copiou.
inline bool migrateFileOnce (const juce::File& newFile, const juce::File& legacyFile)
{
    if (newFile.exists() || ! legacyFile.existsAsFile() || newFile == legacyFile)
        return false;
    newFile.getParentDirectory().createDirectory();
    if (! legacyFile.copyFileTo (newFile))
        return false;
    noteMigration (newFile.getParentDirectory(), legacyFile);
    return true;
}

// Copia o conteúdo de uma pasta antiga para a nova, uma vez só: só se a nova
// está vazia ou não existe. Para DADOS DO APP (pequenos). Material não se
// copia: continua sendo lido onde está. true se copiou.
inline bool migrateDirectoryOnce (const juce::File& newDir, const juce::File& legacyDir)
{
    if (! legacyDir.isDirectory() || newDir == legacyDir)
        return false;
    if (newDir.isDirectory() && newDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
        return false;
    if (legacyDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        return false;
    if (! legacyDir.copyDirectoryTo (newDir))
        return false;
    noteMigration (newDir, legacyDir);
    return true;
}

// Versão da organização da pasta de dados (data-version.txt): 0 se não há.
inline int dataVersion (const juce::String& instrument)
{
    const auto f = appData (instrument).getChildFile ("data-version.txt");
    return f.existsAsFile() ? f.loadFileAsString().trim().getIntValue() : 0;
}

inline void setDataVersion (const juce::String& instrument, int version)
{
    appData (instrument).createDirectory();
    appData (instrument).getChildFile ("data-version.txt").replaceWithText (juce::String (version) + "\n");
}

// As preferências em <data>/RASGO/<Instrument>/<Instrument>.settings. Se o
// arquivo ainda não existe e há um do lugar antigo (legacyFile, por exemplo
// o padrão do JUCE de antes), ele é copiado primeiro.
inline std::unique_ptr<juce::PropertiesFile> openSettings (const juce::String& instrument,
                                                           const juce::File& legacyFile = {},
                                                           juce::PropertiesFile::StorageFormat format
                                                               = juce::PropertiesFile::storeAsXML)
{
    const auto file = appData (instrument).getChildFile (instrument + ".settings");
    if (legacyFile != juce::File{})
        migrateFileOnce (file, legacyFile);
    juce::PropertiesFile::Options o;
    o.applicationName = instrument;
    o.filenameSuffix = ".settings";
    o.storageFormat = format;
    return std::make_unique<juce::PropertiesFile> (file, o);
}

} // namespace rasgo::folders
