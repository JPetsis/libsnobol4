# DDEV Setup for libsnobol4 PHP Binding

## Quick Start

1. **Create local docker-compose override:**
   ```bash
   cp .ddev/docker-compose.core.yaml.example .ddev/docker-compose.local.yaml
   ```

2. **Edit `docker-compose.local.yaml`:**
   Update the `source` paths to match your system:
   ```yaml
   volumes:
     - type: bind
       source: /your/path/to/libsnobol4/core  # Update this!
       target: /var/www/core
       consistency: cached
     - type: bind
       source: /your/path/to/libsnobol4/public  # Update this!
       target: /var/www/public
       consistency: cached
     - type: bind
       source: /your/path/to/libsnobol4  # Update this!
       target: /var/www/project-root
       consistency: cached
   ```

3. **Configure Docker Desktop (macOS/Windows):**
   - Open Docker Desktop → Settings → Resources → File Sharing
   - Add your project root directory (e.g., `/Users/yourname/Projects/libsnobol4`)
   - Apply & Restart

4. **Start DDEV:**
   ```bash
   ddev start
   ```

The PHP extension will be built automatically on start.

### Root composer.json

The repository root no longer has a `composer.json`. Inside DDEV, `bindings/php/composer.json` is used automatically.

## Verification

After successful build, verify the extension is loaded:
```bash
ddev exec php -m | grep snobol
```

You should see `snobol` in the output.

## Troubleshooting

### "Core directory not found"
- Make sure you created `docker-compose.local.yaml` with correct paths
- Verify Docker Desktop has the directory in File Sharing settings
- Run `ddev restart` after making changes

### "config.m4 not found"
- Make sure you're running DDEV from the `bindings/php/` directory
- The `.ddev/` directory should be inside `bindings/php/`

### Build errors
Check the full output with:
```bash
ddev start --verbose
```

## File Structure

```
bindings/php/
├── .ddev/
│   ├── config.yaml                          # Main DDEV config
│   ├── docker-compose.core.yaml             # Volume mounts (empty by default)
│   ├── docker-compose.core.yaml.example     # Template for local override
│   └── docker-compose.local.yaml            # Your local override (git-ignored)
├── config.m4                                # PHP extension build config
├── src/                                     # PHP extension C source
├── stubs/                                   # IDE stubs (classes now in .so)
└── tests/                                   # PHPUnit tests
```

## Notes

- **Never commit `docker-compose.local.yaml`** - it contains your personal paths
- The `.ddev/` directory is inside `bindings/php/` to isolate PHP development
- Core library is mounted read-only to prevent accidental modifications
- Build happens in `/tmp` inside the container for better performance
