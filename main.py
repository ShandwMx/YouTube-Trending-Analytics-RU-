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

# --- ГЛУБОКАЯ СТАТИСТИКА ---
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

# --- ПРОСМОТР ДАННЫХ ---
pd_st.header("📋 Проводник по данным")
if pd_st.checkbox("Показать интерактивную таблицу с данными"):
    search_query = pd_st.text_input("Поиск по названию видео или канала:")
    df_display = df_unique[['title', 'channelTitle', 'view_count', 'likes', 'comment_count']]
    if search_query:
        df_display = df_display[df_display['title'].str.contains(search_query, case=False, na=False) | df_display['channelTitle'].str.contains(search_query, case=False, na=False)]
    pd_st.dataframe(df_display.head(100), width='stretch')
