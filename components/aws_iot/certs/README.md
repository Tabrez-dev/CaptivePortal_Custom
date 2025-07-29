# AWS IoT Certificates Directory

This directory contains the following certificate files for AWS IoT authentication:
- `device-certificate.pem` - Device certificate
- `private-key.pem` - Private key
- `AmazonRootCA1.pem` - AWS Root CA certificate

## Usage
1. Place all three certificate files in this directory
2. Update the AWS IoT configuration in your application to reference these files

## Security Notes
- These files should NOT be committed to version control
- The files are excluded via .gitignore
- Keep private keys secure and never share them
