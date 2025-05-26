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