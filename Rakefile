### USAGE ###

# install the CLI from https://github.com/spark/particle-cli
# install ruby rake with: bundle install
# log into your particle account with: particle login
# to compile program x: rake x
# to compile program x without shortcut: rake compile PROGRAM=x
# to flash latest compile via USB: rake flash
# to flash latest compile via cloud: rake flash DEVICE=name
# to start serial monitor: rake monitor
# to compile & flash: rake x flash
# to compile, flash & monitor: rake x flash monitor

### EXAMPE PROGRAMS ###

task :led => :compile
task :cloudLed => :compile

desc "compile particleSpike examplie"
task :particleSpike => :compile

### SETUP ###

# setup
require 'fileutils'
require 'yaml'

# recommended versions
# see https://docs.particle.io/reference/product-lifecycle/long-term-support-lts-releases/
versions = {
  'p2' => '6.3.2', # not LTS but required for CloudEvent
  'photon' => '2.3.1',  # LTS
  'argon' => '4.2.0',   # LTS
  'boron' => '4.2.0'    # LTS
}

# constants
examples_folder = "examples/"
lib_folder = "lib/"
bin_folder = "bin/"
src_folder = "/src"
local_folder = "local"

# parameters
platform = ENV['PLATFORM'] || 'p2'
version = ENV['VERSION'] || versions[platform]
device = ENV['DEVICE']
bin = ENV['BIN']

### COMPILE ###

desc "compile binary in the cloud"
task :compile do
  # what program are we compiling?
  program = ENV['PROGRAM'] || Rake.application.top_level_tasks.first
  if program == "default"
    next
  end

  # safety checks
  if platform.nil? || platform.strip.empty?
    raise "Error: PLATFORM must not be empty."
  end
  if version.nil? || version.strip.empty?
    raise "Error: VERSION must not be empty."
  end 

  # paths
  workflow_path = File.join(".github", "workflows", "compile-#{program}.yaml")
  unless File.exist?(workflow_path)
    warn "Workflow YAML config file (#{workflow_path}) not found, compiling just from examples folder: "
    src_path = "#{examples_folder}#{program}#{src_folder}"
    lib_path = ""
    aux_files = ""
  else
    workflow = YAML.load_file(workflow_path)
    paths = workflow.dig("jobs", "compile", "strategy", "matrix", "program")[0]
    src_path = paths["src"]
    lib_path = paths["lib"]
    aux_files = paths["aux"]
    if src_path.nil? || src_path.strip.empty?
      src_path = "#{examples_folder}#{program}#{src_folder}"
    end 
  end
  
  # source
  unless Dir.exist?(src_path)
    raise "Error: folder '#{src_path}' does not exist."
  end
  src_files = Dir.glob("#{src_path}/**/*.{h,c,cpp,properties}").join(' ')

  # libs
  unless lib_path.nil? || lib_path.strip.empty?
    paths = lib_path.strip.split(/\s+/).map do |path|
      if Dir.exist?(File.join(path, src_folder)) 
        File.join(path, src_folder)
      elsif Dir.exists?(File.join(lib_folder, path, src_folder)) 
        File.join(lib_folder, path, src_folder)
      else
        raise "Could not find '#{path}' library in root or #{lib_folder} - make sure it exists"
      end
    end.compact
    lib_path = paths.join(' ')
    lib_files = paths.map do |path|
      Dir.glob("#{path}/**/*.{h,c,cpp}").join(' ')
    end
    lib_files = " " + lib_files.join(' ')
  end

  # aux files
  unless aux_files.nil? || aux_files.strip.empty?
    aux_files = " " + aux_files
  end

  # info
  puts "\nINFO: compiling '#{program}' in the cloud for #{platform} #{version}"
  puts " - src path: #{src_path}"
  puts " - lib paths: #{lib_path}"
  puts " - aux files: #{aux_files}"
  puts "\n"
  
  # compile
  sh "particle compile #{platform} #{src_files}#{lib_files}#{aux_files} --target #{version} --saveTo #{bin_folder}#{program}-#{platform}-#{version}.bin", verbose: false
end

### LOCAL COMPILE ###
# todo: implement an :initLocal that takes the ENV['PROGRAM'] to reset the local/ folder
# (see code for this in the Guardfile), and also stores the code base information (i.e. the program name to parse the workflows file) in the local/ folder
# then guardfile for local just looks at the codebase in local and watches the corresponding files
# to refresh

desc "compiles the program in local/ with the local toolchain"
task :compileLocal do
  ENV['MAKE_LOCAL_TARGET'] = "compile-user"
  Rake::Task[:makeLocal].invoke
end

desc "clean the program in local/"
task :cleanLocal do
  ENV['MAKE_LOCAL_TARGET'] = "clean-user"
  Rake::Task[:makeLocal].invoke
end

require 'open3'

desc "runs local toolchain make target"
task :makeLocal do
  # what program are we compiling?
  target = ENV['MAKE_LOCAL_TARGET']
  # info
  puts "\nINFO: making '#{target}' in folder '#{local_folder}' for #{platform} #{version} with local toolchain"

  # local folder
  local_root = File.expand_path(File.dirname(__FILE__)) + "/" + local_folder
  unless File.exist?("#{local_root}/src")
    raise "src directory in local folder '#{local_root}' not found"
  end

  # compiler path
  compiler_root = "#{ENV['HOME']}/.particle/toolchains/gcc-arm"
  unless File.exist?(compiler_root)
    raise "Compiler directory '#{compiler_root}' not found: particle workbench installed?"
  end
  compiler_version = Dir.children(compiler_root).sort.last
  if compiler_version
    puts " - found gcc-arm compiler version #{compiler_version}"
  else
    raise "No compiler found in '#{compiler_root}': particle workbench installed?"
  end
  compiler_path = "#{compiler_root}/#{compiler_version}/bin"
  
  # buildscript path
  buildscript_root = "#{ENV['HOME']}/.particle/toolchains/buildscripts"
  unless File.exist?(buildscript_root)
    raise "Build script directory '#{buildscript_root}' not found: particle workbench installed?"
  end
  buildscript_version = Dir.children(buildscript_root).sort.last
  if buildscript_version
    puts " - found buildscript version #{buildscript_version}"
  else
    raise "No buildscript found in '#{buildscript_root}': particle workbench installed?"
  end
  buildscript_path = "#{buildscript_root}/#{buildscript_version}/Makefile"

  # device os
  device_os_path = "#{ENV['HOME']}/.particle/toolchains/deviceOS/#{version}"
  unless File.exist?(device_os_path)
    raise "Toolchain for target OS version ('#{device_os_path}') is not installed: have you installed this toolchain version? (vscode Particle: Install local compiler toolchain)"
  end
  cmd = "PATH=#{compiler_path}:$PATH /usr/bin/make -f \"#{buildscript_path}\" #{target} PLATFORM=#{platform} APPDIR=\"#{local_root}\" DEVICE_OS_PATH=\"#{device_os_path}\""
  Open3.popen2e(cmd) do |stdin, stdout_and_err, wait_thr|
    stdout_and_err.each { |line| puts line }
    exit_status = wait_thr.value
    abort "Command failed!" unless exit_status.success?
    output_path = "#{local_root}/target/#{platform}/#{local_folder}.bin"
    if File.exist?(output_path)
      FileUtils.cp(output_path, "#{bin_folder}/local-#{platform}-#{version}.bin")
    end
  end
end

### AUTO-COMPILE ###

desc "start automatic recompile of latest program in cloud"
task :guardCloud do
  sh "bundle exec guard -g cloud"
end

desc "start automatic recompile of latest program locally"
task :guardLocal do
  sh "bundle exec guard -g local"
end

### FLASH ###

desc "flash binary over the air or via usb"
task :flash do

  # is a binary selected?
  unless bin.nil? || bin.strip.empty?
    # user provided
    bin_path = "#{bin_folder}#{bin}"
  else
    # find latest
    files = Dir.glob("#{bin_folder}*.bin").select { |f| File.file?(f) }
    if files.empty?
      raise "No .bin files found in #{bin_folder}"
    end
    bin_path = files.max_by { |f| File.mtime(f) }
  end

  # safety check
  unless File.exist?(bin_path)
    raise "Binary file does not exit: #{bin_path}"
  end

  # OTA or serial?
  unless device.nil? || device.strip.empty?
    # over the air
    puts "\nINFO: flashing #{bin_path} to #{device} via the cloud..."
    sh "particle flash #{device} #{bin_path}"
  else
    puts "\nINFO: putting device into DFU mode"
    sh "particle usb dfu"
    puts "INFO: flashing #{bin_path} over USB (requires device in DFU mode = yellow blinking)..."
    sh "particle flash --usb #{bin_path}"
  end
end

### INFO ###

desc "list available devices connected to USB"
task :list do
  puts "\nINFO: querying list of available USB devices..."
  sh "particle usb list"
  sh "particle identify"
end

desc "get MAC address of device connected to USB"
task :mac do
  puts "\nINFO: checking for MAC address...."
  sh "particle serial mac"
end

desc "start serial monitor"
task :monitor do
  puts "\nINFO: connecting to serial monitor..."
  sh "particle serial monitor --follow"
end

task :help => :programs do
  puts "\n**** AVAILABLE TASKS ****\n\n"
  sh "bundle exec rake -T", verbose: false
  puts "\n"
end

task :programs do
  puts "\n**** AVAILABLE PROGRAMS ****\n\n"
  Rake::Task.tasks.each do |task|
    if !task.prerequisites.empty? && task.prerequisites[0] == "compile"
      puts "rake #{task.name}"
    end
  end
  puts "\n"
end

### TOOLS ###

desc "remove .bin files"
task :clean do
  puts "\nINFO: removing all .bin files..."
  sh "rm -f #{bin_folder}/*.bin"
end

desc "setup particle device --> deprecated"
task :setup do
  # this is no longer supported over serial
  puts "INFO: don't do this over serial, go to https://setup.particle.io/ instead now"
end

desc "start repair doctor --> deprecated"
task :doctor do
  # particle doctor is no longer supported
  puts "USE THIS INSTEAD NOW: https://docs.particle.io/tools/doctor/"
end

desc "update device OS"
task :update do
  puts "\nINFO: updating device OS of the device connected on USB to #{version}..."
  puts "Careful, the firmware on the device needs to be compatible with the new OS (tinker is usually safe)."
  print "Are you sure you want to continue? (y/N): "
  answer = STDIN.gets.strip.downcase
  unless answer == 'y' || answer == 'yes'
    exit 1
  end
  sh "particle usb dfu"
  sh "particle update --target #{version}"
end

desc "flash the tinker app"
# commands: digitalWrite "D7,HIGH", analogWrite, digitalRead, analogRead "A0"
task :tinker do
  puts "\nINFO: flashing tinker..."
  sh "particle flash --usb tinker"
end

desc "used by Guardfile to automatically re-compile binaries on code changes"
task :autoCompile, [:program, :platform, :version, :paths] do |t, args|
  puts "\n**** RE-COMPILE AUTOMATICALLY ****"
  puts "program: #{args.program}"
  puts "platform: #{args.platform} #{args.version}"
  puts "modified files:"
  args.paths.each do |path|
    puts " - #{path}"
  end
  sh "bundle exec rake compile PROGRAM=#{args.program} PLATFORM=#{args.platform} VERSION=#{args.version}", verbose: false
  puts "**** RE-COMPILE COMPLETE ****"
end

### UML ###
# to install:
# docker pull plantuml/plantuml-server:jetty
# docker run -d -p 8080:8080 plantuml/plantuml-server:jetty
uml_docker = "plantuml/plantuml-server:jetty"

desc "pull the docker image for plantuml"
task :umlInstall do
  puts "\nINFO: pulling/updating plantuml docker image ..."
  sh "docker pull #{uml_docker}"
end

desc "start the docker container for plantuml"
task :umlStart do
  puts "\nINFO: starting plantUML server..."
  sh "docker run -d -p 8080:8080 #{uml_docker}"
  puts "set your PlantUML server url settings to http://localhost:8080/ or go to http://localhost:8080/ to generate UML diagrams"
end

desc "stop the docker container for plantuml"
task :umlStop do
  puts "\nINFO: stopping plantUML server..."
  sh "docker stop $(docker ps -f \"ancestor=#{uml_docker}\" --format \"{{.ID}}\")"
end
