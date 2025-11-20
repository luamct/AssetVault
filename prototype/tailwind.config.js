/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{js,ts,jsx,tsx}"],
  theme: {
    extend: {
      fontFamily: {
        display: ['"Space Grotesk"', '"Inter"', 'system-ui', 'sans-serif'],
        body: ['"Inter"', 'system-ui', 'sans-serif'],
      },
      colors: {
        tropic: {
          50: '#e8fff4',
          100: '#baffdf',
          200: '#7cf7c5',
          300: '#39e3a8',
          500: '#00c38a',
          700: '#00805c',
        },
        tide: {
          50: '#e0f9ff',
          100: '#b1ecff',
          200: '#74ddff',
          300: '#28c0ff',
          500: '#0091d9',
          700: '#005785',
        },
        coral: {
          50: '#ffe8f2',
          100: '#ffc1d9',
          200: '#ff8cb7',
          300: '#ff5a96',
          500: '#ff2d78',
          700: '#c31254',
        },
        mango: {
          50: '#fff4dd',
          100: '#ffe0a8',
          200: '#ffc46a',
          300: '#ffa834',
          500: '#ff8a00',
          700: '#cc5d00',
        },
        midnight: '#1b1230',
      },
      boxShadow: {
        glass: '0 25px 70px rgba(0, 145, 217, 0.25)',
      },
    },
  },
  plugins: [],
}
