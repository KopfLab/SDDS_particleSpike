# Guardfile
# to install: bundle install
# to prime: rake PROGRAM
# to run: bundle exec guard
# this will then watch whichever program was compiled during priming and re-compile it
# upon changes to its code base (determined by the push->paths setting in the program
# github compile- workflow file)

require 'yaml'

# constants
bin_folder = "bin"

# program
last_bin = Dir.glob(File.join(bin_folder, '*.bin')).select { |f| File.file?(f) }.reject {|f| File.basename(f).start_with?("local")}.max_by { |f| File.mtime(f) }
program, platform, version = File.basename(last_bin).split('-')
version = version.chomp('.bin')
puts "\nINFO: Setting up guard to re-compile '#{program}' for #{platform} #{version} when there are code changes in:"

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

# guard cloud compile
group :cloud do
  guard 'rake', :task => 'autoCompile', :run_on_start => false, wait_for_changes: true, :task_args => [program, platform, version] do
    watch_paths.each do |pattern|
      watch(Regexp.new(pattern))
    end
  end
end

# guard local compile
# note: this often needs a rake cleanLocal if switching to a different program
# note: takes the last cloud compile as basis to figure out which program
# compile locally, this could probably be improved but works for now
local_folder = "local"
group :local do
  #load 'Rakefile'
  guard :shell do
    puts "Setting up #{local_folder}"
    `rm -r #{local_folder}/src`
    FileUtils.mkdir_p("#{local_folder}/src")
    FileUtils.mkdir_p("#{local_folder}/lib")
    watch_paths.each do |pattern|
      # library or source folder?
      if pattern.start_with?("lib/")
        destination = "#{local_folder}/#{pattern}/src"
        FileUtils.mkdir_p(destination)
      else
        destination = "#{local_folder}/src"
      end
      # copy source files
      Dir.glob("#{pattern}/src/**/*.{h,c,cpp}") do |file|
        next unless File.file?(file)
        filename = File.basename(file)
        FileUtils.cp(file,"#{destination}/#{filename}")
      end
      # watch the folders for changes
      puts "Watching #{pattern}"
      watch(Regexp.new(pattern)) do |m|
        next unless File.file?(m[0])
        filename = File.basename(m[0])
        dest = "#{local_folder}/src/#{filename}"
        puts "synching #{m[0]} → #{dest}"
        FileUtils.cp(m[0], dest)
      end
      # watch for compile and rake
      watch(Regexp.new(pattern)) do |modified|
        debounce(1.0) do
          puts "\n\n *** detected changes, compiling after debounce ***"
          # recompile
          system("rake compileLocal")
          # fixme --> should check if copileLocal actually succeeded
          system("rake flash")
        end
      end
    end
  end
end


require 'thread'

# Shared debounce timer (per Guard process)
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