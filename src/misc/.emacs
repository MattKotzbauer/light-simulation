(load-theme 'deeper-blue t)
(add-to-list 'default-frame-alist '(font . "Liberation Mono-11.5"))
(set-face-attribute 'default t :font "Liberation Mono-11.5")

(add-hook 'prog-mode-hook 'superword-mode)

(global-set-key (kbd "M-<up>") 'backward-paragraph)
(global-set-key (kbd "M-<down>") 'forward-paragraph)

(global-set-key (kbd "C-q") 'back-to-indentation)

(global-set-key (kbd "C-2")  (lambda () (interactive) (other-window 1)))
(global-set-key (kbd "C-0")  (lambda () (interactive) (other-window 1)))
(global-set-key (kbd "C-1")  (lambda () (interactive) (other-window -1)))
(global-set-key (kbd "C-9")  (lambda () (interactive) (other-window -1)))


(defun find-build-bat (dir)
  "Recursively search for build.bat file by moving up through directories"
  (let ((parent (file-name-directory (directory-file-name dir))))
    (if (file-exists-p (expand-file-name "build.bat" dir))
	(expand-file-name "build.bat" dir)
      (unless (or (null parent) (string= dir parent))
	(find-build-bat parent)
      )
    )
  )
)


(defun compile-cpp-build-bat ()
  "Find build.bat in current / parent directory and execute cpp"
  (interactive)
  (let ((build-bat (find-build-bat default-directory)))
    (if build-bat
	(progn
	  (message "Found build.bat: %s" build-bat)
	  (compile (concat "cmd.exe /c " build-bat))
          )
      (message "No build.bat found in current / parent directories")
    )
  )
)

(defun copy-buffer-to-clipboard ()
  "Copy entire current buffer to clipboard"
  (interactive)
  (kill-ring-save (point-min) (point-max))
  (message "Buffer copied to clipboard")
  )

(global-set-key (kbd "C-p") 'copy-buffer-to-clipboard)
(global-set-key (kbd "M-m") 'compile-cpp-build-bat)
