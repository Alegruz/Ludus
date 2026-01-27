# This script fetches the latest tag from the volk GitHub repository and prints it.
# Usage: python get_latest_volk_tag.py
import requests

def get_latest_tag():
    url = "https://api.github.com/repos/zeux/volk/tags"
    response = requests.get(url)
    response.raise_for_status()
    tags = response.json()
    if not tags:
        raise Exception("No tags found in volk repository.")
    return tags[0]['name']

if __name__ == "__main__":
    print(get_latest_tag())
