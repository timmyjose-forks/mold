#include "mold.h"

#include <limits>

InputChunk::InputChunk(ObjectFile *file, const ElfShdr &shdr,
                       std::string_view name)
  : file(file), shdr(shdr), name(name),
    output_section(OutputSection::get_instance(name, shdr.sh_type, shdr.sh_flags)) {}

std::string_view InputChunk::get_contents() const {
  return file->get_string(shdr);
}

i64 InputChunk::get_section_idx() const {
  assert(&file->elf_sections.front() <= &shdr &&
         &shdr < &file->elf_sections.back());
  return &shdr - &file->elf_sections.front();
}

i64 InputChunk::get_priority() const {
  return ((i64)file->priority << 32) | get_section_idx();
}

static std::string rel_to_string(u64 r_type) {
  switch (r_type) {
  case R_X86_64_NONE: return "R_X86_64_NONE";
  case R_X86_64_8: return "R_X86_64_8";
  case R_X86_64_16: return "R_X86_64_16";
  case R_X86_64_32: return "R_X86_64_32";
  case R_X86_64_32S: return "R_X86_64_32S";
  case R_X86_64_64: return "R_X86_64_64";
  case R_X86_64_PC8: return "R_X86_64_PC8";
  case R_X86_64_PC16: return "R_X86_64_PC16";
  case R_X86_64_PC32: return "R_X86_64_PC32";
  case R_X86_64_PC64: return "R_X86_64_PC64";
  case R_X86_64_GOT32: return "R_X86_64_GOT32";
  case R_X86_64_GOTPC32: return "R_X86_64_GOTPC32";
  case R_X86_64_GOTPCREL: return "R_X86_64_GOTPCREL";
  case R_X86_64_GOTPCRELX: return "R_X86_64_GOTPCRELX";
  case R_X86_64_REX_GOTPCRELX: return "R_X86_64_REX_GOTPCRELX";
  case R_X86_64_PLT32: return "R_X86_64_PLT32";
  case R_X86_64_TLSGD: return "R_X86_64_TLSGD";
  case R_X86_64_TLSLD: return "R_X86_64_TLSLD";
  case R_X86_64_TPOFF32: return "R_X86_64_TPOFF32";
  case R_X86_64_DTPOFF32: return "R_X86_64_DTPOFF32";
  case R_X86_64_TPOFF64: return "R_X86_64_TPOFF64";
  case R_X86_64_DTPOFF64: return "R_X86_64_DTPOFF64";
  case R_X86_64_GOTTPOFF: return "R_X86_64_GOTTPOFF";
  }
  unreachable();
}

static void overflow_check(InputSection *sec, Symbol &sym, u64 r_type, u64 val) {
  switch (r_type) {
  case R_X86_64_8:
    if (val != (u8)val)
      Error() << *sec << ": relocation " << rel_to_string(r_type)
              << " against " << sym.name << " out of range: "
              << val << " is not in [0, 255]";
    return;
  case R_X86_64_PC8:
    if (val != (i8)val)
      Error() << *sec << ": relocation " << rel_to_string(r_type)
              << " against " << sym.name << " out of range: "
              << (i64)val << " is not in [-128, 127]";
    return;
  case R_X86_64_16:
    if (val != (u16)val)
      Error() << *sec << ": relocation " << rel_to_string(r_type)
              << " against " << sym.name << " out of range: "
              << val << " is not in [0, 65535]";
    return;
  case R_X86_64_PC16:
    if (val != (i16)val)
      Error() << *sec << ": relocation " << rel_to_string(r_type)
              << " against " << sym.name << " out of range: "
              << (i64)val << " is not in [-32768, 32767]";
    return;
  case R_X86_64_32:
    if (val != (u32)val)
      Error() << *sec << ": relocation " << rel_to_string(r_type)
              << " against " << sym.name << " out of range: "
              << val << " is not in [0, 4294967296]";
    return;
  case R_X86_64_32S:
  case R_X86_64_PC32:
  case R_X86_64_GOT32:
  case R_X86_64_GOTPC32:
  case R_X86_64_GOTPCREL:
  case R_X86_64_GOTPCRELX:
  case R_X86_64_REX_GOTPCRELX:
  case R_X86_64_PLT32:
  case R_X86_64_TLSGD:
  case R_X86_64_TLSLD:
  case R_X86_64_TPOFF32:
  case R_X86_64_DTPOFF32:
  case R_X86_64_GOTTPOFF:
    if (val != (i32)val)
      Error() << *sec << ": relocation " << rel_to_string(r_type)
              << " against " << sym.name << " out of range: "
              << (i64)val << " is not in [-2147483648, 2147483647]";
    return;
  case R_X86_64_NONE:
  case R_X86_64_64:
  case R_X86_64_PC64:
  case R_X86_64_TPOFF64:
  case R_X86_64_DTPOFF64:
    return;
  }
  unreachable();
}

static void write_val(u64 r_type, u8 *loc, u64 val) {
  switch (r_type) {
  case R_X86_64_NONE:
    return;
  case R_X86_64_8:
  case R_X86_64_PC8:
    *loc = val;
    return;
  case R_X86_64_16:
  case R_X86_64_PC16:
    *(u16 *)loc = val;
    return;
  case R_X86_64_32:
  case R_X86_64_32S:
  case R_X86_64_PC32:
  case R_X86_64_GOT32:
  case R_X86_64_GOTPC32:
  case R_X86_64_GOTPCREL:
  case R_X86_64_GOTPCRELX:
  case R_X86_64_REX_GOTPCRELX:
  case R_X86_64_PLT32:
  case R_X86_64_TLSGD:
  case R_X86_64_TLSLD:
  case R_X86_64_TPOFF32:
  case R_X86_64_DTPOFF32:
  case R_X86_64_GOTTPOFF:
    *(u32 *)loc = val;
    return;
  case R_X86_64_64:
  case R_X86_64_PC64:
  case R_X86_64_TPOFF64:
  case R_X86_64_DTPOFF64:
    *(u64 *)loc = val;
    return;
  }
  unreachable();
}

void InputSection::copy_buf() {
  if (shdr.sh_type == SHT_NOBITS || shdr.sh_size == 0)
    return;

  // Copy data
  u8 *base = out::buf + output_section->shdr.sh_offset + offset;
  std::string_view contents = get_contents();
  memcpy(base, contents.data(), contents.size());

  // Apply relocations
  if (shdr.sh_flags & SHF_ALLOC)
    apply_reloc_alloc(base);
  else
    apply_reloc_nonalloc(base);
}

// Apply relocations to SHF_ALLOC sections (i.e. sections that are
// mapped to memory at runtime) based on the result of
// scan_relocations().
void InputSection::apply_reloc_alloc(u8 *base) {
  i64 ref_idx = 0;
  ElfRela *dynrel = nullptr;

  if (out::reldyn)
    dynrel = (ElfRela *)(out::buf + out::reldyn->shdr.sh_offset +
                         file->reldyn_offset + reldyn_offset);

  for (i64 i = 0; i < rels.size(); i++) {
    const ElfRela &rel = rels[i];
    Symbol &sym = *file->symbols[rel.r_sym];
    u8 *loc = base + rel.r_offset;

    const SectionFragmentRef *ref = nullptr;
    if (has_fragments[i])
      ref = &rel_fragments[ref_idx++];

    auto write = [&](u64 val) {
      overflow_check(this, sym, rel.r_type, val);
      write_val(rel.r_type, loc, val);
    };

#define S   (ref ? ref->frag->get_addr() \
             : (sym.plt_idx == -1 ? sym.get_addr() : sym.get_plt_addr()))
#define A   (ref ? ref->addend : rel.r_addend)
#define P   (output_section->shdr.sh_addr + offset + rel.r_offset)
#define G   (sym.get_got_addr() - out::got->shdr.sh_addr)
#define GOT out::got->shdr.sh_addr

    switch (rel_types[i]) {
    case R_NONE:
      break;
    case R_ABS:
      write(S + A);
      break;
    case R_ABS_DYN:
      write(S + A);
      *dynrel++ = {P, R_X86_64_RELATIVE, 0, (i64)(S + A)};
      break;
    case R_DYN:
      *dynrel++ = {P, R_X86_64_64, sym.dynsym_idx, A};
      break;
    case R_PC:
      write(S + A - P);
      break;
    case R_GOT:
      write(G + A);
      break;
    case R_GOTPC:
      write(GOT + A - P);
      break;
    case R_GOTPCREL:
      write(G + GOT + A - P);
      break;
    case R_TLSGD:
      write(sym.get_tlsgd_addr() + A - P);
      break;
    case R_TLSGD_RELAX_LE: {
      // Relax GD to LE
      static const u8 insn[] = {
        0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0, // mov %fs:0, %rax
        0x48, 0x8d, 0x80, 0,    0,    0, 0,       // lea x@tpoff, %rax
      };
      memcpy(loc - 4, insn, sizeof(insn));
      *(u32 *)(loc + 8) = S - out::tls_end + A + 4;
      i++;
      break;
    }
    case R_TLSLD:
      write(out::got->get_tlsld_addr() + A - P);
      break;
    case R_TLSLD_RELAX_LE: {
      // Relax LD to LE
      static const u8 insn[] = {
        // mov %fs:0, %rax
        0x66, 0x66, 0x66, 0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0,
      };
      memcpy(loc - 3, insn, sizeof(insn));
      i++;
      break;
    }
    case R_DTPOFF:
      write(S + A - out::tls_begin);
      break;
    case R_TPOFF:
      write(S + A - out::tls_end);
      break;
    case R_GOTTPOFF:
      write(sym.get_gottpoff_addr() + A - P);
      break;
    default:
      unreachable();
    }

#undef S
#undef A
#undef P
#undef G
#undef GOT
  }
}

// This function is responsible for applying relocations against
// non-SHF_ALLOC sections (i.e. sections that are not mapped to memory
// at runtime).
//
// Relocations against non-SHF_ALLOC sections are much easier to
// handle than that against SHF_ALLOC sections. It is because, since
// they are not mapped to memory, they don't contain any variable or
// function and never need PLT or GOT. Non-SHF_ALLOC sections are
// mostly debug info sections.
//
// Relocations against non-SHF_ALLOC sections are not scanned by
// scan_relocations.
void InputSection::apply_reloc_nonalloc(u8 *base) {
  static Counter counter("reloc_nonalloc");
  counter.inc(rels.size());

  i64 ref_idx = 0;

  for (i64 i = 0; i < rels.size(); i++) {
    const ElfRela &rel = rels[i];
    Symbol &sym = *file->symbols[rel.r_sym];

    if (!sym.file || sym.is_placeholder) {
      Error() << "undefined symbol: " << *file << ": " << sym.name;
      continue;
    }

    const SectionFragmentRef *ref = nullptr;
    if (has_fragments[i])
      ref = &rel_fragments[ref_idx++];

    u8 *loc = base + rel.r_offset;

    switch (rel.r_type) {
    case R_X86_64_NONE:
      break;
    case R_X86_64_8:
    case R_X86_64_16:
    case R_X86_64_32:
    case R_X86_64_32S:
    case R_X86_64_64: {
      u64 val = ref ? ref->frag->get_addr() : sym.get_addr();
      overflow_check(this, sym, rel.r_type, val);
      write_val(rel.r_type, loc, val);
      break;
    }
    case R_X86_64_DTPOFF64:
      write_val(rel.r_type, loc, sym.get_addr() + rel.r_addend - out::tls_begin);
      break;
    case R_X86_64_PC8:
    case R_X86_64_PC16:
    case R_X86_64_PC32:
    case R_X86_64_PC64:
    case R_X86_64_GOT32:
    case R_X86_64_GOTPC32:
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
    case R_X86_64_PLT32:
    case R_X86_64_TLSGD:
    case R_X86_64_TLSLD:
    case R_X86_64_DTPOFF32:
    case R_X86_64_TPOFF32:
    case R_X86_64_TPOFF64:
    case R_X86_64_GOTTPOFF:
      Error() << *this << ": invalid relocation for non-allocated sections: "
              << rel.r_type;
      break;
    default:
      Error() << *this << ": unknown relocation: " << rel.r_type;
    }
  }
}

// Linker has to create data structures in an output file to apply
// some type of relocations. For example, if a relocation refers a GOT
// or a PLT entry of a symbol, linker has to create an entry in .got
// or in .plt for that symbol. In order to fix the file layout, we
// need to scan relocations.
void InputSection::scan_relocations() {
  if (!(shdr.sh_flags & SHF_ALLOC))
    return;

  static Counter counter("reloc_alloc");
  counter.inc(rels.size());

  this->reldyn_offset = file->num_dynrel * sizeof(ElfRela);
  this->rel_types.resize(rels.size());

  // Scan relocations
  for (i64 i = 0; i < rels.size(); i++) {
    const ElfRela &rel = rels[i];
    Symbol &sym = *file->symbols[rel.r_sym];
    bool is_readonly = !(shdr.sh_flags & SHF_WRITE);
    bool is_code = (sym.st_type == STT_FUNC);

    if (!sym.file || sym.is_placeholder) {
      Error() << "undefined symbol: " << *file << ": " << sym.name;
      continue;
    }

    auto report_error = [&]() {
      Error() << *this << ": " << rel_to_string(rel.r_type)
              << " relocation against symbol `" << sym.name
              << "' can not be used; recompile with -fPIE";
    };

    switch (rel.r_type) {
    case R_X86_64_NONE:
      rel_types[i] = R_NONE;
      break;
    case R_X86_64_8:
    case R_X86_64_16:
    case R_X86_64_32:
    case R_X86_64_32S:
      if (config.pie && sym.is_relative())
        report_error();
      if (sym.is_imported)
        sym.flags |= is_code ? NEEDS_PLT : NEEDS_COPYREL;
      rel_types[i] = R_ABS;
      break;
    case R_X86_64_64:
      if (config.pie) {
        if (sym.is_imported) {
          if (is_readonly)
            report_error();
          sym.flags |= NEEDS_DYNSYM;
          rel_types[i] = R_DYN;
          file->num_dynrel++;
        } else if (sym.is_relative()) {
          if (is_readonly)
            report_error();
          rel_types[i] = R_ABS_DYN;
          file->num_dynrel++;
        } else {
          rel_types[i] = R_ABS;
        }
      } else {
        if (sym.is_imported)
          sym.flags |= is_code ? NEEDS_PLT : NEEDS_COPYREL;
        rel_types[i] = R_ABS;
      }
      break;
    case R_X86_64_PC8:
    case R_X86_64_PC16:
    case R_X86_64_PC32:
    case R_X86_64_PC64:
      if (sym.is_imported)
        sym.flags |= is_code ? NEEDS_PLT : NEEDS_COPYREL;
      rel_types[i] = R_PC;
      break;
    case R_X86_64_GOT32:
      sym.flags |= NEEDS_GOT;
      rel_types[i] = R_GOT;
      break;
    case R_X86_64_GOTPC32:
      sym.flags |= NEEDS_GOT;
      rel_types[i] = R_GOTPC;
      break;
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
      sym.flags |= NEEDS_GOT;
      rel_types[i] = R_GOTPCREL;
      break;
    case R_X86_64_PLT32:
      if (sym.is_imported || sym.st_type == STT_GNU_IFUNC)
        sym.flags |= NEEDS_PLT;
      rel_types[i] = R_PC;
      break;
    case R_X86_64_TLSGD:
      if (i + 1 == rels.size() || rels[i + 1].r_type != R_X86_64_PLT32)
        Error() << *this << ": TLSGD reloc not followed by PLT32";

      if (config.relax && !sym.is_imported) {
        rel_types[i] = R_TLSGD_RELAX_LE;
        i++;
      } else {
        sym.flags |= NEEDS_TLSGD;
        sym.flags |= NEEDS_DYNSYM;
        rel_types[i] = R_TLSGD;
      }
      break;
    case R_X86_64_TLSLD:
      if (i + 1 == rels.size() || rels[i + 1].r_type != R_X86_64_PLT32)
        Error() << *this << ": TLSLD reloc not followed by PLT32";
      if (sym.is_imported)
        Error() << *this << ": TLSLD reloc refers external symbol " << sym.name;

      if (config.relax) {
        rel_types[i] = R_TLSLD_RELAX_LE;
        i++;
      } else {
        sym.flags |= NEEDS_TLSLD;
        rel_types[i] = R_TLSLD;
      }
      break;
    case R_X86_64_DTPOFF32:
    case R_X86_64_DTPOFF64:
      if (sym.is_imported)
        Error() << *this << ": DTPOFF reloc refers external symbol " << sym.name;
      rel_types[i] = config.relax ? R_TPOFF : R_DTPOFF;
      break;
    case R_X86_64_TPOFF32:
    case R_X86_64_TPOFF64:
      rel_types[i] = R_TPOFF;
      break;
    case R_X86_64_GOTTPOFF:
      sym.flags |= NEEDS_GOTTPOFF;
      rel_types[i] = R_GOTTPOFF;
      break;
    default:
      Error() << *this << ": unknown relocation: " << rel.r_type;
    }
  }
}

static size_t find_null(std::string_view data, u64 entsize) {
  if (entsize == 1)
    return data.find('\0');

  for (i64 i = 0; i <= data.size() - entsize; i += entsize)
    if (data.substr(i, i + entsize).find_first_not_of('\0') ==
        std::string_view::npos)
      return i;

  return std::string_view::npos;
}

// Mergeable sections (sections with SHF_MERGE bit) typically contain
// string literals. Linker is expected to split the section contents
// into null-terminated strings, merge them with mergeable strings
// from other object files, and emit uniquified strings to an output
// file.
//
// This mechanism reduces the size of an output file. If two source
// files happen to contain the same string literal, the output will
// contain only a single copy of it.
//
// It is less common than string literals, but mergeable sections can
// contain fixed-sized read-only records too.
//
// This function splits the section contents into small pieces that we
// call "section fragments". Section fragment is a unit of merging.
//
// We do not support mergeable sections that have relocations.
MergeableSection::MergeableSection(InputSection *isec)
  : InputChunk(isec->file, isec->shdr, isec->name),
    parent(*MergedSection::get_instance(isec->name, isec->shdr.sh_type,
                                        isec->shdr.sh_flags)) {
  std::string_view data = isec->get_contents();
  const char *begin = data.data();
  u64 entsize = isec->shdr.sh_entsize;

  static_assert(sizeof(SectionFragment::alignment) == 2);
  if (isec->shdr.sh_addralign >= (1 << 16))
    Fatal() << *isec << ": alignment too large";

  if (isec->shdr.sh_flags & SHF_STRINGS) {
    while (!data.empty()) {
      size_t end = find_null(data, entsize);
      if (end == std::string_view::npos)
        Error() << *this << ": string is not null terminated";

      std::string_view substr = data.substr(0, end + entsize);
      data = data.substr(end + entsize);

      SectionFragment *frag = parent.insert(substr, isec->shdr.sh_addralign);
      fragments.push_back(frag);
      frag_offsets.push_back(substr.data() - begin);
    }
  } else {
    if (data.size() % entsize)
      Fatal() << *isec << ": section size is not multiple of sh_entsize";

    while (!data.empty()) {
      std::string_view substr = data.substr(0, entsize);
      data = data.substr(entsize);

      SectionFragment *frag = parent.insert(substr, isec->shdr.sh_addralign);
      fragments.push_back(frag);
      frag_offsets.push_back(substr.data() - begin);
    }
  }

  static Counter counter("string_fragments");
  counter.inc(fragments.size());
}
