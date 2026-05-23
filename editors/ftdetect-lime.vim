" Lime grammar files: place this snippet at
"     ~/.vim/ftdetect/lime.vim
" or  ~/.config/nvim/ftdetect/lime.vim
" so vim recognises *.lime as the lime filetype and applies syntax/lime.vim.

autocmd BufRead,BufNewFile *.lime setfiletype lime
autocmd BufRead,BufNewFile *.lex  setfiletype lime
