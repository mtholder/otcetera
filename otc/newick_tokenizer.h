#ifndef OTCETERA_NEWICK_TOKENIZER_H
#define OTCETERA_NEWICK_TOKENIZER_H
#include <iostream>
#include <fstream>
#include <stack>
#include <stdexcept>
#include "otc/otc_base_includes.h"
#include "otc/error.h"
#include "otc/util.h"

namespace otc {

struct ParsingRules {
    ParsingRules()
        :ottIdValidator(nullptr),
        includeInternalNodesInDesIdSets(false) {
        }
    const std::set<long> * ottIdValidator;
    bool includeInternalNodesInDesIdSets;
};

typedef std::shared_ptr<const std::string> ConstStrPtr;
struct FilePosStruct {
    FilePosStruct()
        :pos(0U),
        lineNumber(0U),
        colNumber(0U),
        filepath(ConstStrPtr(nullptr)) {
    }
    FilePosStruct(const ConstStrPtr fp)
        :pos(0U),
        lineNumber(0U),
        colNumber(0U),
        filepath(fp) {
    }
    FilePosStruct(std::size_t position, std::size_t line, std::size_t column, const ConstStrPtr fp)
        :pos(position),
        lineNumber(line),
        colNumber(column),
        filepath(fp) {
    }
    std::string describe() const {
        std::string message;
        message = "At line ";
        message += std::to_string(1 + this->lineNumber);
        message += ", column ";
        message += std::to_string(1 + this->colNumber);
        message += ", filepos ";
        message += std::to_string(1 + this->pos);
        try {
            if (filepath) {
                message += " of \"";
                message += *filepath;
                message += "\"";
            } else {
                message += " of <UNKNOWN FILENAME>";
            }
        } catch (...) {
        }
        return message;
    }

    std::size_t pos;
    std::size_t lineNumber;
    std::size_t colNumber;
    ConstStrPtr filepath;
};

class OTCParsingError: public OTCError {
    public:
        OTCParsingError(const char * msg, char offending, const FilePosStruct & position)
            :OTCError(msg),
            frag(msg),
            pos(position) {
                if (offending != '\0') {
                    offendingStr.assign(1, offending);
                }
                message = this->generate_message();
            }
        OTCParsingError(const char * msg, const std::string & offending, const FilePosStruct & position)
            :OTCError(msg),
            frag(msg),
            offendingStr(offending),
            pos(position) {
                message = this->generate_message();
            }
        std::string generate_message() const noexcept {
            try {
                std::string m = "Error found \"";
                m += this->offendingStr;
                m += "\" ";
                m += this->frag;
                m += " ";
                m += this->pos.describe();
                return m;
            } catch (...) {// to guarantee noexcept...
            }
            return frag;
        }

    private:
        const std::string frag;
        std::string offendingStr;
        const FilePosStruct pos;
};

class OTCParsingContentError: public OTCError {
    public:
        OTCParsingContentError(const char * msg, const FilePosStruct & position)
            :OTCError(msg),
            pos(position) {
                message = this->generate_message(msg);
            }
        std::string generate_message(const char *frag) const noexcept {
            try {
                std::string m = "Error found: ";
                m += frag;
                m += " ";
                m += this->pos.describe();
                return m;
            } catch (...) {// to guarantee noexcept...
            }
            try {
                return std::string{frag};
            } catch (...) {
                return std::string{};
            }
        }
    private:
        const FilePosStruct pos;
};

class NewickTokenizer {
    public:
        enum newick_token_state_t {
                NWK_NOT_IN_TREE, // before first open-parens
                NWK_OPEN, // token was open-parens
                NWK_CLOSE, // token was close-parens
                NWK_COMMA, // token was ,
                NWK_COLON, // token was :
                NWK_BRANCH_INFO, // token was text after :
                NWK_LABEL, // token was text (but not after a :)
                NWK_SEMICOLON
            };
        NewickTokenizer(std::istream &inp, const std::string & filepath)
            :inputStream(inp),
            inpFilepath(nullptr) {
            if (filepath.length() > 0) {
                const std::string * raw = new std::string(filepath);
                inpFilepath = std::shared_ptr<const std::string>(raw);
            }
        }
        class iterator;
        class Token {
            public:
                const std::string & content() const {
                    return this->tokenContent;
                }
                const std::vector<std::string> & commentVec() const {
                    return this->comments;
                }
                const FilePosStruct & getStartPos() const {
                    return this->startPos;
                }
            private:
                Token(const std::string &content,
                      const FilePosStruct & startPosition,
                      const FilePosStruct & endPosition,
                      const std::vector<std::string> &embeddedComments,
                      newick_token_state_t tokenState)
                    :tokenContent(content),
                    startPos(startPosition),
                    endPos(endPosition),
                    comments(embeddedComments),
                    state(tokenState) {
                    LOG(TRACE) << "created token for \"" << content << "\"";
                }
            public:
                const std::string tokenContent;
                const FilePosStruct startPos;
                const FilePosStruct endPos;
                const std::vector<std::string> comments;
                const newick_token_state_t state;

                friend class NewickTokenizer::iterator;
        };

        class iterator : std::forward_iterator_tag {
            public:
                bool operator==(const iterator & other) const {
                    //LOG(TRACE) << "Equality test atEnd = " << this->atEnd << " other.atEnd = " << other.atEnd;
                    if (other.atEnd) {
                        return this->atEnd;
                    }
                    if (this->atEnd) {
                        return false;
                    }
                    return (this->currentPos == other.currentPos) && (&(this->inputStream) == &(other.inputStream));
                }
                bool operator!=(const iterator & other) const {
                    //LOG(TRACE) << "Inequality test atEnd = " << this->atEnd << " other.atEnd = " << other.atEnd << '\n';
                    return !(*this == other);
                }
                Token operator*() const {
                    LOG(TRACE) << "* operator";
                    return Token(currWord, *prevPos, *currentPos, comments, currTokenState);
                }
                iterator & operator++() {
                    LOG(TRACE) << "increment";
                    if (this->atEnd) {
                        throw std::out_of_range("Incremented a dead NewickTokenizer::iterator");
                    }
                    if (this->currTokenState == NWK_SEMICOLON) {
                        this->atEnd = true;
                        this->resetToken();
                    } else {
                        this->resetToken();
                        this->consumeNextToken();
                    }
                    return *this;
                }
            private:
                void consumeNextToken();
                bool advanceToNextNonWhitespace(char &);
                void finishReadingComment();
                void finishReadingUnquoted(bool continuingLabel);
                void finishReadingQuotedStr();
                void onLabelExit(char nextChar, bool enteringFromWhitespace);
                char peek() {
                    if (!pushed.empty()) {
                        return pushed.top();
                    }
                    char c = static_cast<char>(this->inputStream.rdbuf()->sgetc());
                    pushed.push(c);
                    return c;
                }
                void push(char c) {
                    this->pushed.push(c);
                    if (c == '\n') {
                        this->currentPos->colNumber = lastLineInd;
                        this->currentPos->lineNumber -= 1;
                    } else {
                        this->currentPos->colNumber -= 1;
                    }
                    this->currentPos->pos -= 1;
                }
                void throwSCCErr(char c) const __attribute__ ((noreturn));
                //deals with \r\n as \n Hence "LogicalChar"
                bool advanceReaderOneLogicalChar(char & c) {
                    if (!pushed.empty()) {
                        c = pushed.top();
                        pushed.pop();
                    } else {
                        if (this->atEnd) {
                            c = EOF;
                            return false;
                        }
                        c = static_cast<char>((this->inputStream.rdbuf())->sbumpc());
                    }
                    if (c == EOF) {
                        this->atEnd = true;
                    } else {
                        this->currentPos->pos += 1;
                        if (13 == c || 10 == c) {
                            if (13 == c ) { // deal with \r\n as a newline
                                if (this->inputStream.rdbuf()->sgetc() == 10) {//peeks at the next char
                                    (inputStream.rdbuf())->sbumpc();
                                    this->currentPos->pos += 1;
                                }
                            }
                            c = '\n';
                            this->lastLineInd = this->currentPos->colNumber;
                            this->currentPos->colNumber = 0;
                            this->currentPos->lineNumber += 1;
                        } else {
                            this->currentPos->colNumber += 1;
                        }
                    }
                    return !this->atEnd;
                }
                void resetToken() {
                    this->currWord.clear();
                    this->prevTokenState = this->currTokenState;
                    std::swap(currentPos, prevPos);
                    currentPos->pos = prevPos->pos;
                    currentPos->lineNumber = prevPos->lineNumber;
                    currentPos->colNumber = prevPos->colNumber;
                    comments.clear();
                }
                iterator(std::istream &inp, ConstStrPtr filepath)
                    :inputStream(inp),
                    inpFilepath(filepath),
                    atEnd(!inp.good()),
                    firstPosSlot(filepath),
                    secondPosSlot(filepath),
                    currTokenState(NWK_NOT_IN_TREE),
                    prevTokenState(NWK_NOT_IN_TREE),
                    numUnclosedParens(0) {
                    currentPos = &firstPosSlot;
                    prevPos = &secondPosSlot;
                    LOG(TRACE) << "create live";
                    ++(*this);
                }
                iterator(std::istream &inp) // USE in end() ONLY!
                    :inputStream(inp),
                    inpFilepath(nullptr),
                    atEnd(true),
                    firstPosSlot(nullptr),
                    secondPosSlot(nullptr),
                    currTokenState(NWK_NOT_IN_TREE),
                    prevTokenState(NWK_NOT_IN_TREE),
                    numUnclosedParens(0) {
                    LOG(TRACE) << "create dead";

                }
                std::istream & inputStream;
                ConstStrPtr inpFilepath;
                bool atEnd;
                FilePosStruct firstPosSlot;
                FilePosStruct secondPosSlot;
                FilePosStruct * currentPos; // alias
                FilePosStruct * prevPos;    // alias
                std::string currWord;
                newick_token_state_t currTokenState;
                newick_token_state_t prevTokenState;
                long numUnclosedParens;
                std::stack<char> pushed;
                std::vector<std::string> comments;
                std::size_t lastLineInd;
                friend class NewickTokenizer;
        };
        iterator begin() {
            iterator b(this->inputStream, this->inpFilepath);
            return b;
        }
        iterator end() {
            LOG(TRACE) << "NewickTokenizer.end()";
            return iterator(this->inputStream);
        }
    private:
        std::istream & inputStream;
        ConstStrPtr inpFilepath;
};

inline long ottIDFromName(const std::string & n) {
    if (n.empty()) {
        return -1;
    }
    const unsigned long lastInd = n.length() - 1;
    unsigned long currInd = lastInd;
    const char * c = n.c_str();
    if (strchr("0123456789", c[currInd]) == 0) {
        return -2;
    }
    while (currInd > 0) {
        --currInd;
        if (strchr("0123456789", c[currInd]) == 0) {
            ++currInd;
            break;
        }
    }
    long conv = -2;
    auto r = char_ptr_to_long(c + currInd, &conv);
    assert(r);
    return conv;
}

}// namespace otc
#endif
