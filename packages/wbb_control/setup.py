from setuptools import setup
from glob import glob

package_name = 'wbb_control'

setup(
    name=package_name,
    version='0.0.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name, glob('launch/*.yaml')),
    ],
    install_requires=['setuptools', 'websockets'],
    zip_safe=True,
    maintainer='enaix',
    maintainer_email='enaix@protonmail.com',
    description='Whiteboard bot control node',
    license='TODO: License declaration',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'control = wbb_control.control:main'
        ],
    },
)
