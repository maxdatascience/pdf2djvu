/* Copyright © 2007-2015 Jakub Wilk <jwilk@jwilk.net>
 *
 * This file is part of pdfdjvu.
 *
 * pdf2djvu is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * pdf2djvu is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef PDF2DJVU_SYSTEM_HH
#define PDF2DJVU_SYSTEM_HH

#include <cstdarg>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "autoconf.hh"

class OSError : public std::runtime_error
{
protected:
  explicit OSError(const std::string &message)
  : std::runtime_error(message)
  { };
};

class POSIXError : public OSError
{
public:
  static std::string error_message(const std::string &context);
  explicit POSIXError(const std::string &context)
  : OSError(error_message(context))
  { };
};

void throw_posix_error(const std::string &context);
#ifdef WIN32
void throw_win32_error(const std::string &context);
#endif

class NoSuchFileOrDirectory : public POSIXError
{
public:
  NoSuchFileOrDirectory(const std::string &context)
  : POSIXError(context)
  { };
};

class NotADirectory : public POSIXError
{
public:
  NotADirectory(const std::string &context)
  : POSIXError(context)
  { };
};

class File;

class Command
{
protected:
  std::string command;
  std::vector<std::string> argv;
  std::string repr();
  void call(std::istream *stdin_, std::ostream *stdout_, bool stderr_);
public:
  class CommandFailed : public std::runtime_error
  {
  public:
    CommandFailed(const std::string &message)
    : std::runtime_error(message)
    { }
  };
  explicit Command(const std::string& command);
  Command &operator <<(const std::string& arg);
  Command &operator <<(const File& arg);
  Command &operator <<(int i);
  void operator()(std::ostream &stdout_, bool quiet=false)
  {
    this->call(NULL, &stdout_, !quiet);
  }
  void operator()(bool quiet=false)
  {
    this->call(NULL, NULL, !quiet);
  }
  static std::string filter(const std::string &command_line, const std::string string);
};

class Directory
{
protected:
  std::string name;
  void *posix_dir;
  void open(const char *name);
  void close();
  Directory()
  : name(""), posix_dir(NULL)
  { }
public:
  explicit Directory(const std::string &name);
  virtual ~Directory() throw ();
  friend std::ostream &operator<<(std::ostream &, const Directory &);
};

class TemporaryDirectory : public Directory
{
private:
  TemporaryDirectory(const TemporaryDirectory&); // not defined
  TemporaryDirectory& operator=(const TemporaryDirectory&); // not defined
public:
  TemporaryDirectory();
  virtual ~TemporaryDirectory() throw ();
};

class File : public std::fstream
{
private:
  File(const File&); // not defined
  File& operator=(const File&); // not defined
protected:
  std::string name;
  std::string base_name;
  void open(const char* path, bool truncate = true);
  File()
  { }
public:
  explicit File(const std::string &name);
  File(const Directory& directory, const std::string &name);
  virtual ~File() throw ()
  { }
  size_t size();
  void reopen(bool truncate = false);
  const std::string& get_basename() const;
  operator const std::string& () const;
  friend std::ostream &operator<<(std::ostream &, const File &);
};

class TemporaryFile : public File
{
private:
  TemporaryFile(const TemporaryFile &); // not defined
  TemporaryFile& operator=(const TemporaryFile &); // not defined
protected:
  void construct();
public:
  TemporaryFile(const Directory& directory, const std::string &name)
  : File(directory, name)
  { }
  TemporaryFile(const std::string &name)
  : File(name)
  { }
  TemporaryFile();
  virtual ~TemporaryFile() throw ();
};

class ExistingFile : public File
{
private:
  ExistingFile(const ExistingFile &); // not defined
  ExistingFile& operator=(const ExistingFile &); // not defined
public:
  explicit ExistingFile(const std::string &name);
  virtual ~ExistingFile() throw ()
  { }
  ExistingFile(const Directory& directory, const std::string &name);
};

#if WIN32

class Cwd
{
protected:
  std::string previous_cwd;
private:
  Cwd(const Cwd &); // not defined
  Cwd& operator=(const Cwd &); // not defined
public:
  explicit Cwd(const std::string &path);
  ~Cwd();
};

class ProgramDir
: public std::string
{
public:
  ProgramDir();
};

const static ProgramDir program_dir;

#endif

namespace encoding
{

  enum encoding
  {
    native,
    terminal,
    utf8,
  };

  template <enum encoding from, enum encoding to>
  class proxy;

  template <enum encoding from, enum encoding to>
  std::ostream &operator << (std::ostream &, const proxy<from, to> &);

  template <enum encoding from, enum encoding to>
  class proxy
  {
  protected:
    const std::string &string;
  public:
    explicit proxy<from, to>(const std::string &string)
    : string(string)
    { }
    friend std::ostream &operator << <>(std::ostream &, const proxy<from, to> &);
  };

}

void copy_stream(std::istream &istream, std::ostream &ostream, bool seek);
void copy_stream(std::istream &istream, std::ostream &ostream, bool seek, std::streamsize limit);

std::string string_vprintf(const char *message, va_list args);
std::string string_printf(const char *message, ...)
#if defined(__GNUC__)
__attribute__ ((format (printf, 1, 2)))
#endif
;

bool isatty(const std::ostream &ostream);
void binmode(const std::ostream &ostream);

void split_path(const std::string &path, std::string &directory_name, std::string &file_name);

std::string absolute_path(const std::string &path, const std::string &dir_name);

bool is_same_file(const std::string &path1, const std::string &path2);

void prevent_pop_out();

#endif

// vim:ts=2 sts=2 sw=2 et
