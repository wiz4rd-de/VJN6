import './theme.css';
import { mount } from 'svelte';
import App from './App.svelte';
import { app } from './model.svelte';

const target = document.getElementById('app');
if (target) {
  mount(App, { target });
  app.boot();
}
