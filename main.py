import streamlit as pd_st
import pandas as pd
import plotly.express as px

pd_st.set_page_config(page_title="YouTube Trending Analytics", layout="wide")
pd_st.title("📊 Интерактивный анализ трендов YouTube (RU)")
pd_st.markdown("Веб-интерфейс для анализа больших массивов данных.")

@pd_st.cache_data
def load_data():
    df = pd.read_csv('RU_youtube_trending_data.csv', encoding='utf-8-sig')
    df.columns = [str(col).strip() for col in df.columns]
    if len(df.columns) > 0 and df.columns[0] != 'video_id':
        df.rename(columns={df.columns[0]: 'video_id'}, inplace=True)
    df['description'] = df['description'].fillna('No description')
    df['publishedAt'] = pd.to_datetime(df['publishedAt'], errors='coerce')
    df['trending_date'] = pd.to_datetime(df['trending_date'], errors='coerce')
    df = df.dropna(subset=['publishedAt', 'trending_date'])
    df['publish_day'] = df['publishedAt'].dt.day_name()
    df['publish_hour'] = df['publishedAt'].dt.hour
    df['days_to_trend'] = (df['trending_date'] - df['publishedAt']).dt.days
    df.loc[df['days_to_trend'] < 0, 'days_to_trend'] = 0
    return df
with pd_st.spinner('Оптимизация и загрузка датасета YouTube (238k+ строк)...'):
    df = load_data()
    if 'video_id' in df.columns:
        df_unique = df.drop_duplicates(subset='video_id', keep='last')
    else:
        df_unique = df.drop_duplicates(keep='last')

# --- ОСНОВНЫЕ МЕТРИКИ ---
pd_st.header("📈 Общие показатели датасета")
col1, col2, col3, col4 = pd_st.columns(4)
col1.metric("Всего записей в трендах", f"{df.shape[0]:,}")
col2.metric("Уникальных видеороликов", f"{df_unique.shape[0]:,}")
col3.metric("Всего уникальных каналов", f"{df_unique['channelTitle'].nunique():,}")
col4.metric("Максимум просмотров на видео", f"{df_unique['view_count'].max():,}")

pd_st.markdown("---")

# --- ИНТЕРАКТИВНЫЕ ФИЛЬТРЫ И ТОПЫ ---
pd_st.header("🏆 Рейтинги каналов")

top_n = pd_st.slider("Сколько каналов вывести в топ?", min_value=5, max_value=100, value=10)

top_channels = df['channelTitle'].value_counts().head(top_n).reset_index()
top_channels.columns = ['Канал', 'Дней в трендах']

fig_channels = px.bar(
    top_channels, 
    x='Дней в трендах', 
    y='Канал', 
    orientation='h',
    title=f"Топ-{top_n} каналов по времени удержания в трендах",
    color='Дней в трендах',
    color_continuous_scale='Viridis'
)
fig_channels.update_layout(yaxis={'categoryorder':'total ascending'})
pd_st.plotly_chart(fig_channels, width='stretch')

# --- ГЛУБОКАЯ СТАТИСТИКА И КОРРЕЛЯЦИИ ---
pd_st.header("🔍 Углубленный анализ взаимосвязей")

tab1, tab2, tab3 = pd_st.tabs(["Просмотры vs Лайки", "Активность по часам", "Скорость попадания в тренды"])

with tab1:
    pd_st.subheader("Зависимость между просмотрами, лайками и комментариями")
    sample_size = pd_st.selectbox("Размер выборки для графика (для быстроты отрисовки):", [5000, 10000, 50000])
    df_sample = df_unique.sample(n=min(sample_size, len(df_unique)), random_state=42)
    
    fig_scatter = px.scatter(
        df_sample, 
        x='view_count', 
        y='likes', 
        size='comment_count',
        hover_name='title',
        hover_data=['channelTitle'],
        title="Связь Просмотров и Лайков (Размер пузырька = кол-во комментариев)",
        labels={'view_count': 'Просмотры', 'likes': 'Лайки'},
        color_continuous_scale='Reds'
    )
    pd_st.plotly_chart(fig_scatter, width='stretch')

with tab2:
    pd_st.subheader("В какое время авторы публикуют контент?")
    hourly_stats = df_unique['publish_hour'].value_counts().sort_index().reset_index()
    hourly_stats.columns = ['Час публикации', 'Количество видео']
    
    fig_hours = px.line(
        hourly_stats, 
        x='Час публикации', 
        y='Количество видео', 
        markers=True,
        title="Распределение публикаций по часам суток (UTC)"
    )
    pd_st.plotly_chart(fig_hours, width='stretch')

with tab3:
    pd_st.subheader("Через сколько дней видео залетает в тренды?")
    days_stats = df_unique['days_to_trend'].value_counts().head(10).reset_index()
    days_stats.columns = ['Дней до трендов', 'Количество видео']
    
    fig_days = px.bar(
        days_stats, 
        x='Дней до трендов', 
        y='Количество видео',
        title="Скорость попадания видео в топ после релиза",
        color='Количество видео'
    )
    pd_st.plotly_chart(fig_days, width='stretch')

# --- ПРОСМОТР САМИХ ДАННЫХ ---
pd_st.header("📋 Проводник по данным")
if pd_st.checkbox("Показать интерактивную таблицу с данными"):
    search_query = pd_st.text_input("Поиск по названию видео или канала:")
    df_display = df_unique[['title', 'channelTitle', 'view_count', 'likes', 'comment_count']]
    if search_query:
        df_display = df_display[df_display['title'].str.contains(search_query, case=False, na=False) | df_display['channelTitle'].str.contains(search_query, case=False, na=False)]
    pd_st.dataframe(df_display.head(100), width='stretch')

# --- ЭКСПОРТ ---
pd_st.markdown("---")
pd_st.header("💾 Экспорт отчёта")

import plotly.io as pio
from datetime import datetime

fig_channels_html = pio.to_html(fig_channels, full_html=False, include_plotlyjs=True)
fig_scatter_html = pio.to_html(fig_scatter, full_html=False, include_plotlyjs=False)
fig_hours_html = pio.to_html(fig_hours, full_html=False, include_plotlyjs=False)
fig_days_html = pio.to_html(fig_days, full_html=False, include_plotlyjs=False)

html_export = f"""<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>YouTube Trending Analytics — Экспорт {datetime.now().strftime('%Y-%m-%d %H:%M')}</title>
<style>
  body {{ font-family: sans-serif; background: #0e1117; color: #fafafa; margin: 0; padding: 20px; }}
  h1 {{ color: #ff4b4b; }}
  h2 {{ color: #fafafa; border-bottom: 1px solid #333; padding-bottom: 8px; margin-top: 40px; }}
  .subtitle {{ color: #aaa; margin-bottom: 30px; }}
  .metrics {{ display: flex; gap: 20px; flex-wrap: wrap; margin: 20px 0; }}
  .metric {{ background: #1e1e2e; border-radius: 8px; padding: 20px 30px; flex: 1; min-width: 150px; }}
  .metric-label {{ font-size: 13px; color: #aaa; margin-bottom: 6px; }}
  .metric-value {{ font-size: 28px; font-weight: bold; color: #ff4b4b; }}
  .chart {{ background: #1e1e2e; border-radius: 8px; padding: 10px; margin: 20px 0; }}
  .filter-badge {{ display: inline-block; background: #333; border-radius: 4px; padding: 3px 10px; font-size: 12px; color: #aaa; margin-bottom: 10px; }}
  .footer {{ text-align: center; color: #555; margin-top: 60px; font-size: 12px; border-top: 1px solid #222; padding-top: 20px; }}
</style>
</head>
<body>
<h1>📊 Интерактивный анализ трендов YouTube (RU)</h1>
<p class="subtitle">Экспорт от {datetime.now().strftime('%d.%m.%Y %H:%M')} &nbsp;·&nbsp; Выборка: {sample_size:,} видео &nbsp;·&nbsp; Топ каналов: {top_n}</p>

<h2>📈 Общие показатели датасета</h2>
<div class="metrics">
  <div class="metric"><div class="metric-label">Всего записей в трендах</div><div class="metric-value">{df.shape[0]:,}</div></div>
  <div class="metric"><div class="metric-label">Уникальных видеороликов</div><div class="metric-value">{df_unique.shape[0]:,}</div></div>
  <div class="metric"><div class="metric-label">Всего уникальных каналов</div><div class="metric-value">{df_unique['channelTitle'].nunique():,}</div></div>
  <div class="metric"><div class="metric-label">Максимум просмотров на видео</div><div class="metric-value">{df_unique['view_count'].max():,}</div></div>
</div>

<h2>🏆 Рейтинги каналов</h2>
<div class="filter-badge">Топ-{top_n} каналов</div>
<div class="chart">{fig_channels_html}</div>

<h2>🔍 Просмотры vs Лайки</h2>
<div class="filter-badge">Выборка: {sample_size:,} видео</div>
<div class="chart">{fig_scatter_html}</div>

<h2>🕐 Активность по часам публикации</h2>
<div class="chart">{fig_hours_html}</div>

<h2>⚡ Скорость попадания в тренды</h2>
<div class="chart">{fig_days_html}</div>

<div class="footer">YouTube Trending Analytics &nbsp;·&nbsp; Сгенерировано {datetime.now().strftime('%d.%m.%Y %H:%M')}</div>
</body>
</html>"""

pd_st.download_button(
    label="📥 Скачать страницу как index.html",
    data=html_export,
    file_name="index.html",
    mime="text/html",
    use_container_width=True
)
