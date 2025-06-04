// 全局变量和配置
let autoRefreshInterval = null;
const DEFAULT_REFRESH_INTERVAL = 3000; // 3秒
const BASE_URL = window.location.origin;

// 通用的 API 获取函数
async function fetchWithTimeout(url, options = {}, timeout = 5000) {
    const controller = new AbortController();
    const id = setTimeout(() => controller.abort(), timeout);
    
    try {
        const response = await fetch(url, {
            ...options,
            signal: controller.signal
        });
        
        clearTimeout(id);
        
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        
        return await response.json();
    } catch (error) {
        clearTimeout(id);
        console.error(`Fetch error for ${url}:`, error);
        throw error;
    }
}

// 初始化函数
function initializeDashboard() {
    // 设置默认设置
    setupDefaultSettings();
    
    // 初始化数据获取
    fetchEnvironmentData();
    fetchRecognizedFaces();
    
    // 启动自动刷新
    startAutoRefresh();
}

// 设置默认系统设置
function setupDefaultSettings() {
    // 默认设置初始化
    document.getElementById('autoRefreshToggle').checked = true;
    document.getElementById('refreshInterval').value = 3;
    document.getElementById('darkModeToggle').checked = false;
}

// 获取环境数据
function fetchEnvironmentData() {
    fetchWithTimeout(`${BASE_URL}/api/data`)
        .then(data => {
            const d = data.data || data;

            updateEnvironmentCard('Temperature', d.Temperature, '°C');
            updateEnvironmentCard('Humidity', d.Humidity, '%');
            updateEnvironmentCard('lux', d.lux, ' lux');
            updateEnvironmentCard('co2', d.co2, ' ppm');

            updateStatusPanel(d);
        }) // ✅ 关键：正确闭合 .then()
        .catch(error => {
            console.error('获取环境数据失败:', error);
            updateErrorState('environment');
        });
}

function getCurrentTime() {
    const now = new Date();
    return `今天 ${now.getHours()}:${now.getMinutes().toString().padStart(2, '0')}`;
  }

function addAlert(icon, title, message) {
    const alertsDiv = document.getElementById("recentAlerts");

    const alertHTML = `
      <div class="alert-item">
        <div class="alert-title">
          <span>${icon}</span>
          <span>${title}</span>
        </div>
        <div class="alert-time">${getCurrentTime()}</div>
        <div class="alert-message">${message}</div>
      </div>
    `;

    alertsDiv.insertAdjacentHTML('beforeend', alertHTML);
  }

// 更新错误状态
function updateErrorState(type) {
    const errorMessage = '获取数据失败';
    switch(type) {
        case 'environment':
            ['Temperature', 'Humidity', 'lux', 'co2'].forEach(id => {
                const valueElement = document.getElementById(id);
                const trendElement = document.getElementById(`${id.toLowerCase()}Trend`);
                if (valueElement) valueElement.textContent = '--';
                if (trendElement) trendElement.textContent = errorMessage;
            });
            break;
        case 'faces':
            const peopleContainer = document.getElementById('peopleContainer');
            peopleContainer.innerHTML = `<p style="color:red;">${errorMessage}</p>`;
            break;
    }
}

// 更新环境数据卡片
function updateEnvironmentCard(cardId, value, unit) {
    const valueElement = document.getElementById(cardId);
    const trendElement = document.getElementById(`${cardId.toLowerCase()}Trend`);
        console.log(`[调试] 正在更新 ${cardId}，值为:`, value);
    if (valueElement && trendElement) {
        // 如果有值，则更新
        if (value !== undefined) {
            valueElement.textContent = `${value}${unit}`;
            // 趋势判断（这里简单地模拟趋势）
            trendElement.innerHTML = value > 50 ? 
                `<span class="trend-up">↑ 上升</span>` : 
                `<span class="trend-down">↓ 下降</span>`;
        } else {
            valueElement.textContent = '--';
            trendElement.textContent = '--';
        }
         console.warn(`[警告] 元素未找到：${cardId} 或 ${cardId.toLowerCase()}Trend`);
    }
}

// 更新状态面板
function updateStatusPanel(data) {
    // 烟雾状态
    const smokeIcon = document.getElementById('smokeIcon');
    const smokeLabel = document.getElementById('smokeLabel');
    if (data.mq2 !== undefined) {
        smokeLabel.textContent = data.mq2 > 0 ? '烟雾：异常' : '烟雾：正常';
        smokeIcon.classList.toggle('alert', data.mq2 > 0);
    }

    // 门窗状态 - 根据实际情况调整
    const doorLabel = document.getElementById('doorLabel');
    doorLabel.textContent = '门窗：未知'; // 你可能需要从数据中获取实际状态

        // 自动判断并生成警报
    const alerts = generateEnvironmentSuggestions(data);
    const alertsDiv = document.getElementById("recentAlerts");

    // 删除旧的动态警报（保留静态的）
    const staticItems = Array.from(alertsDiv.children).filter(el => el.dataset.static === "true");
    alertsDiv.innerHTML = '';
    staticItems.forEach(item => alertsDiv.appendChild(item));

    alerts.forEach(([icon, title, message]) => {
        addAlert(icon, title, message);
    });

}

// 获取识别的人脸
function fetchRecognizedFaces() {
    fetchWithTimeout(`${BASE_URL}/api/faces`)
        .then(data => {
            const peopleContainer = document.getElementById('peopleContainer');
            
            // 清除加载中提示
            peopleContainer.innerHTML = '';
            
            // 如果有识别到的人脸
            if (data.names && data.names.length > 0) {
                data.names.forEach(name => {
                    const peopleCard = document.createElement('div');
                    peopleCard.className = 'people-card';
                    peopleCard.innerHTML = `
                        <div class="people-status ${name === 'stranger' ? 'away' : ''}"></div>
                        <h4>👤 ${name === 'stranger' ? '陌生人' : name}</h4>
                        <p>${name === 'stranger' ? '未识别' : '已识别'}</p>
                    `;
                    peopleContainer.appendChild(peopleCard);
                });
            } else {
                peopleContainer.innerHTML = '<p>当前无人员活动</p>';
            }
        })
        .catch(error => {
            console.error('获取人脸识别数据失败:', error);
            updateErrorState('faces');
        });
}
async function getEnvironmentAdvice() {
    const adviceBox = document.getElementById('adviceBox');
    const adviceContent = document.getElementById('adviceContent');
    adviceContent.innerHTML = '<div class="loading"><div class="spinner"></div> 正在分析环境数据...';
    adviceBox.style.display = 'block';

    try {
        const response = await fetch('/advice');
        const data = await response.json();

        if (data.advice) {
            adviceContent.innerHTML = formatAdvice(data.advice);
        } else {
            adviceContent.innerHTML = '<div class="alert-item">无法获取建议，请稍后再试。</div>';
        }
    } catch (error) {
        console.error('获取建议失败:', error);
        adviceContent.innerHTML = '<div class="alert-item">获取建议时出错，请检查网络连接。</div>';
    }
    adviceBox.style.display = 'flex';

}




function formatAdvice(text) {
    // 将文本转换为带样式的HTML
    const items = text.split('\n').filter(item => item.trim());
    return items.map(item => {
        let icon = '✅';
        if (item.includes('⚠️')) icon = '⚠️';
        if (item.includes('❌')) icon = '❌';
        return `<div class="advice-item">${icon} ${item.replace(/[✅⚠️❌]/g, '').trim()}</div>`;
    }).join('');
}

// 启动自动刷新
function startAutoRefresh() {
    // 清除之前的定时器
    if (autoRefreshInterval) {
        clearInterval(autoRefreshInterval);
    }

    const autoRefreshToggle = document.getElementById('autoRefreshToggle');
    const refreshIntervalInput = document.getElementById('refreshInterval');

    if (autoRefreshToggle.checked) {
        const interval = parseInt(refreshIntervalInput.value) * 1000 || DEFAULT_REFRESH_INTERVAL;
        
        autoRefreshInterval = setInterval(() => {
            fetchEnvironmentData();
            fetchRecognizedFaces();
        }, interval);
    }
}

// 切换标签页功能
function switchTab(containerId, tabId) {
    const container = document.getElementById(containerId);
    const tabs = container.querySelectorAll('.tab');
    const tabContents = container.querySelectorAll('.tab-content');

    tabs.forEach(tab => tab.classList.remove('active'));
    tabContents.forEach(content => content.classList.remove('active'));

    document.querySelector(`#${containerId} .tab[onclick*="${tabId}"]`).classList.add('active');
    document.getElementById(tabId).classList.add('active');
}

// 打开模态框
function openModal(modalId) {
    const modal = document.getElementById(modalId);
    modal.style.display = 'flex';
}

// 关闭模态框
function closeModal(modalId) {
    const modal = document.getElementById(modalId);
    modal.style.display = 'none';
}


// 切换暗黑模式
function toggleDarkMode() {
    const darkModeToggle = document.getElementById('darkModeToggle');
    document.body.classList.toggle('dark-mode', darkModeToggle.checked);
}

// 事件监听器
document.addEventListener('DOMContentLoaded', () => {
    // 初始化仪表板
    initializeDashboard();

    // 自动刷新设置事件
    document.getElementById('autoRefreshToggle').addEventListener('change', startAutoRefresh);
    document.getElementById('refreshInterval').addEventListener('change', startAutoRefresh);
    setInterval( getEnvironmentAdvice, 3000000);
    getEnvironmentAdvice(); // 页面加载时立即执行一次
});
  // 你的判断逻辑封装为函数
  function generateEnvironmentSuggestions(data) {
    const suggestions = [];

    // 温湿指数
    if (data.Temperature !== undefined && data.Humidity !== undefined) {
      const T = parseFloat(data.Temperature);
      const RH = parseFloat(data.Humidity) / 100;
      const I = +(T - 0.55 * (1 - RH) * (T - 14.4)).toFixed(1);

      if (I < 14.0) {
        suggestions.push(["🥶", "温湿指数偏低", "当前温湿指数较低，感觉寒冷，建议保暖，开启取暖设备。"]);
      } else if (I < 17.0) {
        suggestions.push(["🧥", "温湿指数偏低", "当前温湿指数偏低，感觉较冷，注意保暖。"]);
      } else if (I <= 25.4) {
        suggestions.push(["😊", "温湿指数适中", "当前温湿指数适中，环境舒适。"]);
      } else if (I <= 27.5) {
        suggestions.push(["🌡️", "温湿指数偏高", "当前温湿指数偏高，有热感，建议适当通风。"]);
      } else {
        suggestions.push(["🥵", "温湿指数过高", "当前温湿指数过高，感到闷热，建议开启空调通风。"]);
      }
    } else {
      suggestions.push(["⚠️", "温湿指数异常", "无法计算温湿指数，数据不完整。"]);
    }

    // CO₂ 判断
    if (data.co2 > 3000) {
      suggestions.push(["⚠️", "CO₂ 超标", "CO₂ 浓度严重超标，建议立即通风！"]);
    } else if (data.co2 > 2000) {
      suggestions.push(["⚠️", "CO₂ 较高", "CO₂ 浓度较高，请加强通风。"]);
    }

    // 烟雾判断
    if (data.mq2 > 0) {
      suggestions.push(["🔥", "烟雾警报", "检测到烟雾，请检查是否有火源或燃气泄漏！"]);
    }

    return suggestions;
  }
    const alerts = generateEnvironmentSuggestions(sensorData);
  alerts.forEach(([icon, title, message]) => {
    addAlert(icon, title, message);
  });
// 让 adviceBox 可拖动
(function makeAdviceBoxDraggable() {
  const box = document.getElementById('adviceBox');
  const header = box.querySelector('.drag-header');
  let offsetX = 0, offsetY = 0, isDragging = false;

  header.onmousedown = function (e) {
    isDragging = true;
    offsetX = e.clientX - box.offsetLeft;
    offsetY = e.clientY - box.offsetTop;
    document.onmousemove = function (e) {
      if (isDragging) {
        box.style.left = (e.clientX - offsetX) + 'px';
        box.style.top = (e.clientY - offsetY) + 'px';
        box.style.right = 'auto'; // 解锁右边定位
        box.style.bottom = 'auto'; // 解锁底部定位
      }
    };
    document.onmouseup = function () {
      isDragging = false;
    };
  };
})();

