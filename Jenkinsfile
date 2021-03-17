pipeline {
    agent {
        docker {
            image 'controller.trafficserver.org/ats/old_ci/fedora:30'
            registryUrl 'https://controller.trafficserver.org/'
            label 'docker'
        }
    }
    stages {
        stage('Clone') {
            steps {
                dir('src') {
                    checkout([$class: 'GitSCM',
                        branches: [[name: sha1]],
                        extensions: [],
                        userRemoteConfigs: [[url: 'https://github.com/ezelkow1/trafficserver', refspec: '+refs/pull/*:refs/remotes/origin/pr/*']]])
                    sh 'head -1 README'
                }
                echo 'Finished Cloning'
            }
        }
        stage('Build') {
            steps {
                echo 'Starting build'
                dir('src') {
                    sh('head -1 README')
                    sh('ls')
                    sh('autoreconf -fiv')
                    //sh('./configure --enable-experimental-plugins')
                    sh('./configure')
                    sh('make -j4')
                }
            }
        }
    }
    
    post { 
        cleanup { 
            cleanWs()
        }
    }
}
