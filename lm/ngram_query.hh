#ifndef LM_NGRAM_QUERY_H
#define LM_NGRAM_QUERY_H

#include "lm/enumerate_vocab.hh"
#include "lm/model.hh"
#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/usage.hh"

#include <cstdlib>
#include <string>
#include <cmath>

namespace lm {
namespace ngram {

class BasicPrint {
  public:
    explicit BasicPrint(int fd, bool flush = true) : out_(fd), flush_(flush) {}
    void Word(StringPiece, WordIndex, const FullScoreReturn &) {}
    void Line(uint64_t oov, float total) {
      out_ << "Total: " << total << " OOV: " << oov << '\n';
      if (flush_) out_.flush();
    }
    void Summary(double, double, uint64_t, uint64_t) {}
  protected:
    util::FakeOFStream out_;
    bool flush_;
};

class FullPrint : public BasicPrint {
  public:
    explicit FullPrint(int fd, bool flush = true) : BasicPrint(fd, flush) {}

    void Word(StringPiece surface, WordIndex vocab, const FullScoreReturn &ret) {
      out_ << surface << '=' << vocab << ' ' << static_cast<unsigned int>(ret.ngram_length)  << ' ' << ret.prob << '\t';
      if (flush_) out_.flush();
    }

    void Summary(double ppl_including_oov, double ppl_excluding_oov, uint64_t corpus_oov, uint64_t corpus_tokens) {
      out_ <<
        "Perplexity including OOVs:\t" << ppl_including_oov << "\n"
        "Perplexity excluding OOVs:\t" << ppl_excluding_oov << "\n"
        "OOVs:\t" << corpus_oov << "\n"
        "Tokens:\t" << corpus_tokens << '\n';
      out_.flush();
    }
};

template <class Model, class Printer> void Query(const Model &model, bool sentence_context) {
  Printer printer(1);
  typename Model::State state, out;
  lm::FullScoreReturn ret;
  StringPiece word;

  util::FilePiece in(0);

  double corpus_total = 0.0;
  double corpus_total_oov_only = 0.0;
  uint64_t corpus_oov = 0;
  uint64_t corpus_tokens = 0;

  while (true) {
    state = sentence_context ? model.BeginSentenceState() : model.NullContextState();
    float total = 0.0;
    uint64_t oov = 0;

    while (in.ReadWordSameLine(word)) {
      lm::WordIndex vocab = model.GetVocabulary().Index(word);
      ret = model.FullScore(state, vocab, out);
      if (vocab == model.GetVocabulary().NotFound()) {
        ++oov;
        corpus_total_oov_only += ret.prob;
      }
      total += ret.prob;
      printer.Word(word, vocab, ret);
      ++corpus_tokens;
      state = out;
    }
    // If people don't have a newline after their last query, this won't add a </s>.
    // Sue me.
    try {
      UTIL_THROW_IF('\n' != in.get(), util::Exception, "FilePiece is confused.");
    } catch (const util::EndOfFileException &e) { break; }
    if (sentence_context) {
      ret = model.FullScore(state, model.GetVocabulary().EndSentence(), out);
      total += ret.prob;
      ++corpus_tokens;
      printer.Word("</s>", model.GetVocabulary().EndSentence(), ret);
    }
    printer.Line(oov, total);
    corpus_total += total;
    corpus_oov += oov;
  }
  printer.Summary(
      pow(10.0, -(corpus_total / static_cast<double>(corpus_tokens))), // PPL including OOVs
      pow(10.0, -((corpus_total - corpus_total_oov_only) / static_cast<double>(corpus_tokens - corpus_oov))), // PPL excluding OOVs
      corpus_oov,
      corpus_tokens);
}

template <class Model> void Query(const char *file, const Config &config, bool sentence_context, bool show_words) {
  Model model(file, config);
  if (show_words) {
    Query<Model, FullPrint>(model, sentence_context);
  } else {
    Query<Model, BasicPrint>(model, sentence_context);
  }
}

} // namespace ngram
} // namespace lm

#endif // LM_NGRAM_QUERY_H


