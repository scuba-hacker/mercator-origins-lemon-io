Import("env")
import subprocess
import os

def run_html_converter():
    """Run the existing HTML conversion script before build"""
    project_dir = env.get("PROJECT_DIR")
    script_path = os.path.join(project_dir, "tools", "update_html.sh")
    
    if not os.path.exists(script_path):
        print(f"Warning: {script_path} not found, skipping HTML conversion")
        return
    
    try:
        # Run the existing conversion script
        result = subprocess.run([script_path], 
                              cwd=project_dir, 
                              capture_output=True, 
                              text=True, 
                              check=True)
        
        # Print the script output
        if result.stdout:
            print(result.stdout.strip())
            
    except subprocess.CalledProcessError as e:
        print(f"Error running HTML converter: {e}")
        if e.stdout:
            print(f"stdout: {e.stdout}")
        if e.stderr:
            print(f"stderr: {e.stderr}")
    except Exception as e:
        print(f"Error running HTML converter: {e}")

# Convert HTML to header before build
run_html_converter()

env.Replace(PROGNAME="mercator-origins-lemon-io")
