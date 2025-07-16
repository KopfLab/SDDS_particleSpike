### USAGE ###

# install the CLI from https://github.com/spark/particle-cli
# install ruby rake with: bundle install
# log into your particle account with: particle login
# to compile program x: rake x
# to compile program x locally: rake local x
# to compile program x without shortcut: rake compile PROGRAM=x
# to flash latest compile via USB: rake flash
# to flash latest compile via cloud: rake flash DEVICE=name
# to start serial monitor: rake monitor
# to compile & flash: rake x flash
# to compile, flash & monitor: rake x flash monitor

### SETUP ###

# setup
require 'fileutils'
require 'yaml'
require 'digest'

# recommended versions
# see https://docs.particle.io/reference/product-lifecycle/long-term-support-lts-releases/
@versions ||= {
  'p2' => '6.3.2',      # not LTS but required for CloudEvent
  'photon' => '2.3.1',  # LTS
  'argon' => '4.2.0',   # LTS
  'boron' => '6.3.2',   # not LTS but required for CloudEvent
  'msom' => '6.3.2'     # not LTS but required for CloudEvent
}

# constants
@programs_folder ||= "programs"
@lib_folder ||= "lib"
@bin_folder ||= "bin"
@src_folder ||= "src"
@local_folder ||= "local"

# parameters
@platform = ENV['PLATFORM'] || @platform || 'p2'
@version = ENV['VERSION'] || @version || @versions[@platform]
@device = ENV['DEVICE'] || @device
@bin_file = ENV['BIN'] || @bin_file

### COMPILE ###

desc "compile binary in the cloud (default) or locally"
task :compile do
  # check for local compile
  local = Rake.application.top_level_tasks.first == "local"
  # what program are we compiling?
  if local
    if Rake.application.top_level_tasks.length < 2
      raise "Error: compiling locally but no second task"
    end
    program = ENV['PROGRAM'] || Rake.application.top_level_tasks[1]
  else
    program = ENV['PROGRAM'] || Rake.application.top_level_tasks.first
  end
  
  # safety checks
  if program == "default"
    next
  end
  if @platform.nil? || @platform.strip.empty?
    raise "Error: PLATFORM must not be empty."
  end
  if @version.nil? || @version.strip.empty?
    raise "Error: VERSION must not be empty."
  end 

  # paths
  workflow_path = File.join(".github", "workflows", "compile-#{program}.yaml")
  unless File.exist?(workflow_path)
    warn "Workflow YAML config file (#{workflow_path}) not found, compiling just from examples folder: "
    src_path = "#{@programs_folder}/#{program}/#{@src_folder}"
    lib_path = ""
    aux_files = ""
  else
    workflow = YAML.load_file(workflow_path)
    paths = workflow.dig("jobs", "compile", "strategy", "matrix", "program")[0]
    src_path = paths["src"]
    lib_path = paths["lib"]
    aux_files = paths["aux"]
    if src_path.nil? || src_path.strip.empty?
      src_path = "#{@programs_folder}/#{program}/#{@src_folder}"
    end 
  end
  
  # source
  unless Dir.exist?(src_path)
    raise "Error: folder '#{src_path}' does not exist."
  end
  src_files = Dir.glob("#{src_path}/**/*.{h,c,cpp}")
  properties_files = Dir.glob("#{src_path}/../project.properties")
  all_files = src_files + properties_files
  dest_files = all_files.map { 
    |path| File.join(@local_folder, @src_folder, File.basename(path)) 
  }

  # libs
  unless lib_path.nil? || lib_path.strip.empty?
    lib_paths = lib_path.strip.split(/\s+/).map do |path|
      if Dir.exist?(File.join(path, @src_folder)) 
        File.join(path, @src_folder)
      elsif Dir.exists?(File.join(@lib_folder, path, @src_folder)) 
        File.join(@lib_folder, path, @src_folder)
      else
        raise "Could not find '#{path}' library in root or #{@lib_folder} - make sure it exists"
      end
    end.compact
    lib_files = lib_paths.map { |path|
      Dir.glob("#{path}/**/*.{h,c,cpp}") 
    }.flatten
    all_files = all_files + lib_files
    dest_files = dest_files + lib_files.map {
      |path| File.join(@local_folder, @lib_folder, path.gsub("lib/", ""))
    }
  end

  # aux files
  unless aux_files.nil? || aux_files.strip.empty?
    aux_files = aux_files.strip.split(/\s+/).map do |path|
      if Dir.exist?(path) 
        Dir.glob("#{path}/**/*.{h,c,cpp}")
      else
        path
      end
    end.compact.flatten
    all_files = all_files + aux_files
    dest_files = dest_files + aux_files.map { 
      |path| File.join(@local_folder, @src_folder, File.basename(path)) 
    }
  end

  # info
  puts "\nINFO: compiling '#{program}' #{local ? 'locally' : 'in the cloud'} for #{@platform} #{@version}"
  puts " - src path: #{src_path}"
  puts " - lib paths: #{lib_paths.join(' ')}" if lib_paths && lib_paths.is_a?(Array)
  puts " - aux files: #{aux_files.join(' ') }" if aux_files && aux_files.is_a?(Array)
  puts "\n"
  
  # compile locally
  if local
    puts "INFO: setting up '#{@local_folder}' folder for compiling"
    if all_files.length != dest_files.length
      raise "Error: incompatible number of source and dest files"
    end
    existing_files = Dir.glob(["#{@local_folder}/src/**/*", "#{@local_folder}/lib/**/*"]).select { |f| File.file?(f) }
    # sync files
    all_files.zip(dest_files).each do |source_path,target_path|
      if File.exist?(target_path)
        # Compare checksums
        source_digest = Digest::SHA256.file(source_path).hexdigest
        target_digest = Digest::SHA256.file(target_path).hexdigest

        if source_digest != target_digest
          puts " - updating: #{target_path}"
          FileUtils.cp(source_path, target_path)
        end
      else
        puts " - adding: #{target_path}"
        unless Dir.exists?(File.dirname(target_path))
          FileUtils.mkdir_p(File.dirname(target_path))
        end
        FileUtils.cp(source_path, target_path)
      end
    end
    # remove redundant files
    redundant_files = existing_files - dest_files
    redundant_files.each do |path| 
      puts " - removing: #{path}"
      FileUtils.rm_rf(path)
    end
    puts "\nINFO: '#{@local_folder}' folder is ready"
    ENV['MAKE_LOCAL_PROGRAM'] = program
    Rake::Task[:compileLocal].invoke
  else
    # compile in cloud
    sh "particle compile #{@platform} #{all_files.join(' ')} --target #{@version} --saveTo #{@bin_folder}/#{program}-#{@platform}-#{@version}-cloud.bin", verbose: false
  end
end

desc "flag compile to be done locally (rake local MYPROG)"
task :local do
   if Rake.application.top_level_tasks.length < 2
      raise "Error: compiling locally but no second task provided"
    end
end

### LOCAL COMPILE ###

desc "compiles the program in local/ with the local toolchain"
task :compileLocal do
  ENV['MAKE_LOCAL_TARGET'] = "compile-user"
  Rake::Task[:makeLocal].invoke
end

desc "cleans the program in local/"
task :cleanLocal do
  ENV['MAKE_LOCAL_TARGET'] = "clean-user"
  Rake::Task[:makeLocal].invoke
end

require 'open3'

desc "runs local toolchain make target"
task :makeLocal do
  # what program are we compiling?
  target = ENV['MAKE_LOCAL_TARGET']
  program = ENV['MAKE_LOCAL_PROGRAM'] || "local"
  # info
  puts "\nINFO: making '#{target}' in folder '#{@local_folder}' for #{program} #{@platform} #{@version} with local toolchain"

  # local folder
  local_root = File.expand_path(File.dirname(__FILE__)) + "/" + @local_folder
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
  device_os_path = "#{ENV['HOME']}/.particle/toolchains/deviceOS/#{@version}"
  unless File.exist?(device_os_path)
    raise "Toolchain for target OS version ('#{device_os_path}') is not installed: have you installed this toolchain version? (vscode Particle: Install local compiler toolchain)"
  end
  cmd = "PATH=#{compiler_path}:$PATH /usr/bin/make -f \"#{buildscript_path}\" #{target} PLATFORM=#{@platform} APPDIR=\"#{local_root}\" DEVICE_OS_PATH=\"#{device_os_path}\""
  Open3.popen2e(cmd) do |stdin, stdout_and_err, wait_thr|
    stdout_and_err.each { |line| puts line }
    exit_status = wait_thr.value
    if exit_status.success?
      output_path = "#{local_root}/target/#{@platform}/#{@local_folder}.bin"
      if File.exist?(output_path)
        bin_path = "#{@bin_folder}/#{program}-#{@platform}-#{@version}-local.bin"
        puts "INFO: saving bin in #{bin_path}"
        FileUtils.cp(output_path, bin_path)
      end
    else
      puts "\n*** COMPILE FAILED ***"
      puts "If you switched programs, platforms, or versions, you may need to clean: rake cleanLocal PLATFORM=#{@platform} VERSION=#{@version}\n\n"
    end
  end
end

### AUTO-COMPILE ###

desc "start automatic recompile of latest program"
task :autoCompile do
  if Rake.application.top_level_tasks.include?("autoFlash")
    ENV['FLASH'] = 'true'
  end
  sh "bundle exec guard"
end

desc "automatic recompile with flash vis USB, use with autoCompile"
task :autoFlash do
end

### FLASH ###

desc "flash binary over the air or via usb"
task :flash do

  # is a binary selected?
  unless @bin_file.nil? || @bin_file.strip.empty?
    # user provided
    bin_path = "#{@bin_folder}/#{@bin_file}"
  else
    # find latest
    files = Dir.glob("#{@bin_folder}/*.bin").select { |f| File.file?(f) }
    if files.empty?
      raise "No .bin files found in #{@bin_folder}"
    end
    bin_path = files.max_by { |f| File.mtime(f) }
  end

  # safety check
  unless File.exist?(bin_path)
    raise "Binary file does not exit: #{bin_path}"
  end

  # OTA or serial?
  unless @device.nil? || @device.strip.empty?
    # over the air
    puts "\nINFO: flashing #{bin_path} to #{@device} via the cloud..."
    sh "particle flash #{@device} #{bin_path}"
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
task :cleanBin do
  puts "\nINFO: removing all .bin files..."
  sh "rm -f #{@bin_folder}/*.bin"
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
  puts "\nINFO: updating device OS of the device connected on USB to #{@version}..."
  puts "Careful, the firmware on the device needs to be compatible with the new OS (tinker is usually safe)."
  print "Are you sure you want to continue? (y/N): "
  answer = STDIN.gets.strip.downcase
  unless answer == 'y' || answer == 'yes'
    exit 1
  end
  sh "particle usb dfu"
  sh "particle update --target #{@version}"
end

desc "flash the tinker app"
# commands: digitalWrite "D7,HIGH", analogWrite, digitalRead, analogRead "A0"
task :tinker do
  puts "\nINFO: flashing tinker..."
  sh "particle flash --usb tinker"
end
