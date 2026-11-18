# see particle_rake.rb for details

### OVERWRITE DEFAULTS ###
@programs_folder = "examples"

### STANDARD RAKE TARGETS ###
import 'particle_rake.rb'

### PROGRAMS ###

desc "SDDS example without particleSpike"
task :sdds => :compile

desc "LED example with particleSpike"
task :led => :compile

desc "Timed switch example"
task :timed_switch => :compile

desc "ADC example with particleSpike"
task :adc => :compile

desc "template with particleSpike"
task :template => :compile

desc "example for project in src"
task :this => :compile

### TOOLS ###

desc "serve the SDDS browser for bluetooth & serial (assets/sddsBrowser.html) on http://localhost:PORT (default 8000) and open it; reloads automatically when the file changes"
task :sddsBrowser do
  require 'socket'
  file = File.join(__dir__, "assets", "sddsBrowser.html")
  port = (ENV['PORT'] || 8000).to_i
  url = "http://localhost:#{port}/"

  # injected into the served page only (not into the file): poll the file's modification time, reload on change
  live_reload = <<~HTML
    <script>/* injected by rake sddsBrowser: reload when the file changes */
    (() => { let last = null; setInterval(async () => {
      try { const t = await (await fetch("/__mtime", { cache: "no-store" })).text();
            if (last !== null && t !== last) location.reload(); last = t; } catch (e) {}
    }, 1000); })();
    </script>
  HTML

  respond = lambda do |client, status, type, body|
    client.write "HTTP/1.1 #{status}\r\nContent-Type: #{type}\r\nContent-Length: #{body.bytesize}\r\n" \
                 "Cache-Control: no-store\r\nConnection: close\r\n\r\n"
    client.write body
  end

  # bind to localhost only: Web Bluetooth requires a secure context, which http://localhost is
  server = TCPServer.new("127.0.0.1", port)
  puts "INFO: serving #{file}"
  puts "INFO: open #{url} in Chrome/Edge (auto-reloads when the file changes), Ctrl+C to stop"
  %w[INT TERM].each { |sig| trap(sig) { puts "\nINFO: stopped"; exit } }

  # open the page (prefer Chrome on macOS since Safari has no Web Bluetooth)
  unless ENV['NO_OPEN']
    if RUBY_PLATFORM.include?("darwin")
      system("open", "-a", "Google Chrome", url, err: File::NULL) || system("open", url)
    elsif system("which xdg-open > /dev/null 2>&1")
      system("xdg-open", url)
    end
  end

  loop do
    Thread.new(server.accept) do |client|
      begin
        path = client.gets.to_s.split(" ")[1].to_s.split("?").first
        while (line = client.gets) && line != "\r\n"; end # skip headers
        case path
        when "/", "/sddsBrowser.html"
          html = File.read(file)
          html = html.include?("</body>") ? html.sub("</body>", live_reload + "</body>") : html + live_reload
          respond.call(client, "200 OK", "text/html; charset=utf-8", html)
        when "/__mtime"
          respond.call(client, "200 OK", "text/plain", File.mtime(file).to_f.to_s)
        else
          respond.call(client, "404 Not Found", "text/plain", "not found")
        end
      rescue => e
        warn "WARNING: #{e.message}"
      ensure
        client.close
      end
    end
  end
end
