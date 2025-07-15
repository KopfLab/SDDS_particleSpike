# Guardfile
# to install: bundle install
# to prime: 
#   cloud: rake PROGRAM
#   local: rake local PROGRAM
# to run: 
#   without flash: rake autoComplile
#   with flash: rake autoCompile autoFlash
# this will then watch whichever program was compiled during priming and re-compile it
# upon changes to its code base (determined by the push->paths setting in the program
# github compile- workflow file)
# tldr; run:
#   rake local PROGRAM autoCompile autoFlash

require 'yaml'

# constants
bin_folder = "bin"
local_folder = "local"

# program
last_bin = Dir.glob(File.join(bin_folder, '*.bin')).select { |f| File.file?(f) }.max_by { |f| File.mtime(f) }
program, platform, version, local = File.basename(last_bin).split('-')
local = local == 'local.bin'
flash = ENV['FLASH'] == 'true'
puts "\nINFO: Setting up guard to re-compile automatically based on last bin"
puts " - last bin: #{last_bin}"
puts " - program: #{program} for #{platform} #{version}"
puts " - autoflash: #{flash ? 'YES' : 'NO'}"
puts " - compile: #{local ? 'LOCAL' : 'in the CLOUD'} when there are code changes in:"

# workflow
workflow_path = File.join(".github", "workflows", "compile-#{program}.yaml")
unless File.exist?(workflow_path)
  raise "Workflow YAML config file not found: #{workflow_path}"
end
workflow = YAML.load_file(workflow_path)

# watch paths from workflow -> on -> push -> paths
watch_paths = workflow.dig(true, "push", "paths").grep_v(/^\.github/) # note: "on" was interpreted as true
watch_paths.each do |path|
  puts " - #{path}"
end
puts "\n"


# guard
guard :shell do
  watch_paths.each do |pattern|
    # watch for compile and rake
    watch(Regexp.new(pattern)) do |modified|
      debounce() do
        puts "\n\n *** detected changes, compiling after debounce ***"

        cmd = "rake#{local ? ' local' : ''} compile PROGRAM=#{program} PLATFORM=#{platform} VERSION=#{version}"
        Open3.popen2e(cmd) do |stdin, stdout_and_err, wait_thr|
          stdout_and_err.each { |line| puts line }
          exit_status = wait_thr.value
          if exit_status.success? && flash
            puts "INFO: flashing to device"
            system("rake flash")
          end
        end
      end
    end
  end
end

# debounce timer (default 1s)
require 'thread'
$debounce_timer = nil
$debounce_mutex = Mutex.new

def debounce(delay = 1.0)
  $debounce_mutex.synchronize do
    if $debounce_timer
      $debounce_timer.kill
    end

    $debounce_timer = Thread.new do
      sleep delay
      yield
    end
  end
end