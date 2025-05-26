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
last_bin = Dir.glob(File.join(bin_folder, '*.bin')).select { |f| File.file?(f) }.max_by { |f| File.mtime(f) }
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

# guard
guard 'rake', :task => 'autoCompile', :run_on_start => false, wait_for_changes: true, :task_args => [program, platform, version] do
  watch_paths.each do |pattern|
    watch(Regexp.new(pattern))
  end
end