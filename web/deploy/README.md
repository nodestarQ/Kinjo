# Deploy

One Docker container serves the web app and runs the relayer (`web/Dockerfile`). nginx on the VPS passes the domain to it and adds HTTPS.

## On the VPS (Ubuntu, Docker and nginx installed)

1. DNS: an A record for the domain pointing at the VPS.
2. The container:

   ```sh
   git clone https://github.com/nodestarQ/Kinjo.git && cd Kinjo/web
   cp .env.example .env            # domain, contract address, relayer key
   chmod 600 .env
   docker compose up -d --build
   curl http://127.0.0.1:8787/health   # or your KINJO_PORT
   ```

3. nginx and HTTPS:

   ```sh
   sudo cp deploy/nginx-kinjo.conf /etc/nginx/sites-available/kinjo   # set the domain
   sudo ln -s /etc/nginx/sites-available/kinjo /etc/nginx/sites-enabled/kinjo
   sudo nginx -t && sudo systemctl reload nginx
   sudo certbot --nginx -d <domain>
   ```

Check: `https://<domain>/health` shows the relayer, `https://<domain>` the app.

Update later: `git pull && docker compose up -d --build`.

On Linux, where the browser's Web Serial fails, open the site with `?helper=ws://127.0.0.1:8765` and run `firmware/tools/serial_helper.py` on the laptop with the boards.
