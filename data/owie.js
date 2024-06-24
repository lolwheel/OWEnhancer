// TODO: trigger relaod on OWIE WiFi Changes after , lets say 5 secs?
// TODO: code cleanup index.html 
// TODO: refactor CSS...

let currentChargingState = null;
let statsDomWritten = false;
let updateInterval = 1000; // in ms
let currentSeverityOffset = 0;
let processUpdate = false;
// TODO: probably make the offset(s) configurable?
// offset for warning severity when battery cells are unbalanced
const batteryCellOffsetWarning = 0.4;

// temperature gauge visual severity class offsets
const TEMPERATURE_SERVERITY_OFFSETS = {
  "45": "warning",
  "55": "error"
}

/**
 * Router Object.
 * Handles URL routing
 */
const Router = {
    lastHash: null,
    //Initializer function. Call this to change listening for window changes.
    init: function () {
      //Remove previous event listener if set
      if (this.listener !== null) {
        window.removeEventListener('popstate', this.listener);
        this.listener = null;
      }
      //Set new listener for "popstate"
      this.listener = window.addEventListener('popstate', function () {
        //Callback to Route checker on window state change
        this.checkRoute.call(this);
      }.bind(this));
      //Call initial routing as soon as thread is available
      setTimeout(function () {
        this.checkRoute.call(this);
      }.bind(this), 0);
      return this;
    },
    //Adding a route to the list
    addRoute: function (name, url, cb) {
      var route = this.routes.find(function (r) {
        return r.name === name;
      });
      url = url.replace(/\//ig, '/');
      if (route === void 0) {
        this.routes.push({
          callback: cb,
          name: name.toString().toLowerCase(),
          url: url
        });
      } else {
        route.callback = cb;
        route.url = url;
      }
      return this;
    },
    //Adding multiple routes to list
    addRoutes: function (routes) {
      var _this = this;
      if (routes === void 0) {
        routes = [];
      }
      routes
        .forEach(function (route) {
          _this.addRoute(route.name, route.url, route.callback);
        });
      return this;
    },
    //Removing a route from the list by route name
    removeRoute: function (name) {
      name = name.toString().toLowerCase();
      this.routes = this.routes
        .filter(function (route) {
          return route.name != name;
        });
      return this;
    },
    //Check which route to activate
    checkRoute: function () {
      //Get hash
      var hash = window.location.hash.substring(1).replace(/\//ig, '/');
      //Default to first registered route.
      var route = this.routes[0];
      //Check each route
      for (var routeIndex = 0; routeIndex < this.routes.length; routeIndex++) {
        var routeToTest = this.routes[routeIndex];
        if (routeToTest.url == hash) {
          route = routeToTest;
          break;
        }
      }
  
      // test no reroute
      if (hash === this.lastHash) {
        console.info("This is the same Route!");
        //Reset destroy task list
        // this.scopeDestroyTasks = [];
        return;
      }
  
      //Run all destroy tasks
      this.scopeDestroyTasks
        .forEach(function (task) {
          task();
        });
      //Reset destroy task list
      this.scopeDestroyTasks = [];
      //Fire route callback
      // set this hast to the lastHash property, so we can find out if we need to trigger the route
      this.lastHash = hash;
      route.callback.call(window);
    },
    //Register scope destroy tasks
    onScopeDestroy: function (cb) {
      this.scopeDestroyTasks.push(cb);
      return this;
    },
    //Tasks to perform when view changes
    scopeDestroyTasks: [],
    //Registered Routes
    routes: [],
    //Listener handle for window events
    listener: null,
    scopeDestroyTaskID: 0
    // return Router;
    // generic Routing Controller
     
};

// Monitoring Object
// Handles the monitoring of the raw bms output via WebSocket
const Monitoring = {
  packets: [],
  lastError: '',
  unknownData: '',
  term: null,
  button: null,
  socket: null,
  init: function () {
    this.term = document.getElementById("term");
    this.button = document.getElementById('startstop');
  },
  formatPacket: function (byteArray) {
    let s = '';
    byteArray.forEach((byte) => {
      s += ('0' + byte.toString(16)).slice(-2);
      s += ' ';
    });
    return s;
  },

  updateTerminal: function () {
    let text = '';
    for (let i = 0; i < this.packets.length; i++) {
      text += this.packets[i] || '';
      text += '\n';
    }
    this.term.value = (text + this.unknownData + this.lastError);
  },

  stop: function () {
    this.socket.close();
    this.socket = undefined;
  },

  connect: function () {
    let _self = this;
    this.socket = new WebSocket(`ws://${window.location.hostname}/rawdata`);
    this.socket.binaryType = "arraybuffer";
    this.socket.onopen = function () {
      _self.lastError = 'connected';
      _self.updateTerminal();
      _self.button.innerText = 'Stop';
      _self.button.setAttribute('onclick', 'Monitoring.stop();');
    }
    this.socket.onclose = function () {
      _self.lastError = 'disconnected';
      _self.updateTerminal();
      // Packets will get erased right after we reconnect.
      _self.packets = [];
      _self.unknownData = '';
      _self.button.innerText = 'Connect';
      _self.button.setAttribute('onclick', 'Monitoring.connect();');
    }
    this.socket.onmessage = function (event) {
      if (!event.data instanceof ArrayBuffer) {
        _self.lastError = "non-binary data";
        _self.updateTerminal();
        return;
      }
      const data = new Uint8Array(event.data);
      if (data[0] == 0) {
        _self.unknownData = `Unknown data: ${_self.formatPacket(data.slice(1))}\n`;
        _self.updateTerminal();
        return;
      }
      if (data.length < 4) {
        _self.lastError = `data is too short, length = ${data.length}`;
        _self.updateTerminal();
        return;
      }
      _self.packets[data[3]] = _self.formatPacket(data);
      _self.updateTerminal();
    };

    this.socket.onerror = function (error) {
      _self.lastError = "websocket error";
      _self.updateTerminal();
    };
  }
};

// toggles the responsive navbar
let toggleResponsiveNav = (forceClose = false) => {
  let nav = document.getElementById("navbar");
  if (forceClose) {
    nav.classList.remove("responsive");
    return;
  }
  nav.classList.contains("responsive") ? nav.classList.remove("responsive") : nav.classList.add("responsive");
}

// render battery cell table from template html
let renderBatteryCellTable = () => {
  let batteryContainer = document.querySelector('.home.content-container .table-container');
  let template = document.getElementById("battery-cell-template");
  for (let i = 0; i < 15; i++) {
    let rowTpl = template.content.cloneNode(true) ;
    rowTpl.firstElementChild.classList.add(`cell-${i}`);
    batteryContainer.append(rowTpl);
  }
}

// toggles given element visibility used in the generic routing controller
let handleElementVisibility = (elementId, visible = true) => {
  const overlay = document.getElementById(elementId);
  overlay.style.display = (visible) ? "block" : "none";
  overlay.style.opacity = (visible) ? "1" : "0";
  // render error fallback 
  setTimeout(() => {
    overlay.style.display = (visible) ? "block" : "none";
  }, 1);
}

// default routing controller used by routes
let genericRouterController = function (elementIdName, callback) {
    return function initialize() {
      toggleResponsiveNav(true);
      handleElementVisibility(elementIdName, true);
      //Destroy elements on exit
      Router.onScopeDestroy(genericExitController);
      if (callback) {
        callback('init');
      }
    };
    //Unloading function
    function genericExitController() {
      handleElementVisibility(elementIdName, false);
      if (callback) {
        callback('exit');
      }
    }
};

// registering routes
Router.addRoutes([
  {
    name: "Home",
    url: "",
    callback: genericRouterController('home')
  },
  {
    name: "Settings",
    url: "settings",
    callback: genericRouterController('settings', (state) => {
      let defaultNav = document.querySelector('.navbar .default');
      let settingsNav = document.querySelector('.navbar .settings');
      let init = (state === 'init');
      settingsNav.style.display = (init)?"flex":"none";
      settingsNav.classList.toggle("active", init);
      defaultNav.style.display = (!init)?"flex":"none";
      defaultNav.classList.toggle("active", !init);
    })
  },
  {
    name: "Stats",
    url: "stats",
    callback: genericRouterController('stats')
  },
  {
    name: "Monitor",
    url: "monitor",
    callback: genericRouterController('monitor', (state) => {
      if (state === 'init') {
        Monitoring.init();
        Monitoring.connect();
      } else {
        Monitoring.stop()
      }
    })
  },
  {
    name: "Development WiFi Connect",
    url: "wifi",
    callback: genericRouterController('wifi')
  },

]);

// OWIE API Endpoint helper
let callOwieApi = async (requestType, endpoint, formData, uploadProgressCallback = null) => {
  return new Promise((resolve, reject) => {
    let xhr = new XMLHttpRequest();
    xhr.open(requestType, `/${endpoint}`);
    // handle responses
    xhr.addEventListener('load', () => {(xhr.status === 200)?resolve(xhr.response):reject(xhr.response)});
    xhr.addEventListener('error', reject);
    if (uploadProgressCallback) {
      xhr.upload.addEventListener('progress', uploadProgressCallback);
    }
    xhr.send(formData);
  })
}

// handle Error
let handleError = (error) => {
  let errorMsg = error?.message || error || "undefined error";
  console.error(error);
  showAlerter("error", errorMsg);

}

// check if there are emty fields in given form and decorate them
let handleEmptyFormFields = (form) => {
  let hasEmptyValues = false;
  for (let i = 0, element; element = form.elements[i++];) {
    element.classList.remove("required");
    if (element.value === "" && element.type !== "submit") {
      element.classList.add("required");
      hasEmptyValues = true;
    }
  }
  return hasEmptyValues;
}

// handle WiFi Passwort clear text
let toggleWifiPwVisibility = (target) => {
  // since we know that the svg icon is after input, we can just get the previous sibling.
  let passwordElement = target.previousElementSibling;
  let type = passwordElement.getAttribute('type') === 'password' ? 'text' : 'password';
  passwordElement.setAttribute('type', type);
}

// handling board arming visibility
let handleBordArmingVisibility = () => {
  const lockingContent = document.querySelector(".board-locking.content");

  if (lockingContent.dataset.canenable) {
    const inactiveContent = document.querySelector(".board-locking.content .bl-inactive");
    inactiveContent.classList.add('hidden');
    updateBoardLockingButtons();
  }
}

// API call to save the WiFi Settings
let updateWifiSettings = async (e) => {
  if (e.preventDefault) e.preventDefault();
  let form = document.querySelector("#wifi-settings");
  // check for empty values, set error decoration and return if empty fields are found!
  if (handleEmptyFormFields(form)) return false;
  try {
    await callOwieApi("POST", "settings", new FormData(form));
    showAlerter("success",  "Update was successful!<br>OWIE is rebooting...");
  } catch(e) {
    handleError(e);
  }
  // We must return false to prevent the default form behavior
  return false;
}

// API call to save the WiFi Dev Settings
let updateWifiDevSettings = async (e) => {
  if (e.preventDefault) e.preventDefault();
  let form = document.querySelector("#wifi-dev");
  // check for empty values, set error decoration and return if empty fields are found!
  if (handleEmptyFormFields(form)) return false;
  try {
    await callOwieApi("POST", "wifi", new FormData(form));
    showAlerter("success",  "WiFi Settings saved!<br>OWIE is rebooting...");
  } catch(e) {
    handleError(e);
  }
  // We must return false to prevent the default form behavior
  return false;
}

// API call to unlock the board
let disarmBoard = async (btn) => {
  try {
    await callOwieApi("GET", "lock?unlock", null);
    showAlerter("success","Successfully unlocked!<br>Please restart your Board!");
    btn.parentElement.style.display = "none";
  } catch (e) {
    handleError(e);
  }
}

// API Call to reset battery stats/settings
let resetBatteryStats = async (type) => {
  try {
    let formData = new FormData();
    formData.append("type", type);
    await callOwieApi("POST", "battery", formData);
    showAlerter("success","Successfully resetted battery "+type+" !");
  } catch (e) {
    handleError(e);
  }
}


// handling the alerter toaster
let showAlerter = (alertType, alertText, showClose=true) => {
  const alerter = document.getElementById("toaster");
  const alertMsg = document.getElementById("alert-msg");
  // first hide an eventually open Alerter
  alerter.classList.remove("show");
  // now we clear the alerter classes
  alerter.classList.remove("success", "error", "warning");
  // add new class
  alerter.classList.add(alertType);
  // Preset status and set text
  alertMsg.innerHTML = alertType.replace(/^./, alertType[0].toUpperCase()) + "<br>" + alertText;
  // unset close if needed
  document.getElementById("toaster-close").style.display = showClose?"block":"none";
  // show it now
  alerter.classList.add("show");
}

// handle board arming/locking buttons and content in settings
let updateBoardLockingButtons = function () {
  const lockingContent = document.querySelector(".board-locking.content");
  const contentDisabled = document.querySelector(".board-locking.content .bl-disabled")
  const contentEnabled = document.querySelector(".board-locking.content .bl-enabled");
  const isEnabled = !!lockingContent.dataset.enabled;
  contentDisabled.classList.toggle('hidden', isEnabled);
  contentEnabled.classList.toggle('hidden', !isEnabled);
}
  
// API call for toggling board arming
let toggleArmingBoard = async () => {
  try {
    let data = await callOwieApi('GET', "lock?toggleArm", null);
    const lockingContent = document.querySelector(".board-locking.content");
    lockingContent.dataset.enabled = data;
    updateBoardLockingButtons();
  } catch (e) {
    console.error(e);
    handleError(e);
  }
}

// toggles the battery info panel in the home screen
let toggleBatteryInfo = () => {
  const batHeader = document.querySelector(".battery-statistics");
  batHeader.classList.toggle("open", !batHeader.classList.contains("open"));
}

// handle the icons shown in the battery gauge bar (toggles battery and outlet icons)
let handleChargingIcons = (charging) => {
  currentChargingState = charging;
    document.querySelectorAll(".owie-loading-bars .loading-icon.outlet").forEach((el) => {
      el.classList.toggle('hidden', !charging);
    });
    document.querySelectorAll(".owie-loading-bars .loading-icon.battery").forEach((el) => {
      el.classList.toggle('hidden', charging);
  })
};

// adds logic for the battery severity calculation 
// and set severity to dom
let handleBatterySeverityDisplay =(data) => {
  let maxCellVoltage = Math.max(...data.battery_cells.value);
  let minCellVoltage = Math.min(...data.battery_cells.value);
  let cellOffset = maxCellVoltage-minCellVoltage;

  // only set serverity if we have a change
  if (currentSeverityOffset !== cellOffset) {
      currentSeverityOffset = cellOffset;
      let serverityElement = document.querySelector(".battery-severity")
      if (cellOffset < batteryCellOffsetWarning) {
          // OK
          serverityElement.innerHTML = "OK";
          serverityElement.classList.remove("warning");
      } else {
          // warning
          serverityElement.innerHTML = "Imbalance";
          serverityElement.classList.add('warning');
      }
  }
}

// fetch metadata from API and set it to the DOM
let handleMetadata = async () => {
  try {
    let data = await callOwieApi("GET", "metadata", null);
    let meta = JSON.parse(data);
     // handling loading icons
     handleChargingIcons(meta.charging);
     // set values to dom
     // owie-version
     document.querySelectorAll(".owie-version-meta").forEach(ove => {
       ove.innerHTML = `${meta.owie_version}`;
     })
   
     // owie AP name
     document.querySelector(".loading-name").innerHTML = `${meta.display_ap_name}`;
     
     // owie - wifi to be connected to (dev feature...)
     document.getElementById('wifi-dev-name').value = meta.ssid;
     document.getElementById('wifi-dev-pw').value = meta.pass;
     
     // owie ap settings
     document.getElementById('apselfname').value = meta.ap_self_name;
     document.getElementById('wifipw').value = meta.ap_password;
     let wifiSelect = document.getElementById('wifi-power');
     wifiSelect.value = meta.wifi_power;
     // generate wifi power options!
     meta.wifi_power_options.forEach((element) => {
       wifiSelect.add(new Option(element.value, element.value,false,(element.selected)));
     });
 
     // board arming
     const lockingContent = document.querySelector(".board-locking.content");
     lockingContent.dataset.canenable = meta.can_enable_locking;
     lockingContent.dataset.enabled = meta.locking_enabled;
     handleBordArmingVisibility();
 
     // handle locking itself
     if (meta.is_locked == "1") {
       document.querySelector(".boardlocking-enabled").style.display = "flex";
     }
     
     // power cycles
     document.querySelector(".power-cycles-meta").innerHTML = `${meta.graceful_shutdown_count}`;
     
     // package stats
     let statsTableBody = document.querySelector(".package-stats");
     meta.package_stats.stats.forEach(element => {
       // build table row and add it to dom!
       // Insert a row at the end of table
       var newRow = statsTableBody.insertRow();
       ["id","period", "deviation", "count"].forEach(cellData => {
           // Insert a cell at the end of the row
         var newCell = newRow.insertCell();
         // Append a text node to the cell
         newCell.appendChild(document.createTextNode(element[cellData] || 0));
       })
     });
     document.querySelector(".unkown-bytes").innerHTML = meta.package_stats.unknown_bytes || 0;
     document.querySelector(".checksum-mismatches").innerHTML = meta.package_stats.missmatches || 0;

     // handle battery cell table renderning
     renderBatteryCellTable();

     // add captured BMS serial number (information only)
     document.querySelector(".bms-serial-meta").innerHTML = `${meta.bms_serial_captured}`;

  } catch (e) {
    handleError(e);
  }
}

// fetch updates from API and set it to the DOM 
// this is currently set to every second and can be configured by setting "updateInterval"
let getAutoupdate = async () => {
  try {
    if (processUpdate) {
      console.info("Firmwareupdate is in progress, autoupdate is suspended until reload of the page!");
      return;
    }
    let data = await callOwieApi("GET", "autoupdate", null);
    let jsonData = JSON.parse(data);
  
    // severity handling
    handleBatterySeverityDisplay(jsonData);

    // handle charging icons only if the status changes..
    if (jsonData.charging.value !== currentChargingState) {
      handleChargingIcons(jsonData.charging.value);
    }

    document.getElementsByClassName("uptime")[0].getElementsByClassName("value-text")[0].innerText = jsonData.uptime.value;
    let style = document.getElementsByClassName("owie-app")[0].style;
    let props = {
      "--owie-percentage-int": jsonData.owie_percentage.value,
      "--bms-percentage-int": jsonData.bms_percentage.value,
      "--current": jsonData.current.value,
    };
    
    // Update sessions max-current and min-current if owie delivers a higher/lower value as defined
    ['--min-current', '--max-current'].forEach((prop)=>{
      if (Math.abs(jsonData.current.value) > Math.abs(getComputedStyle(document.getElementsByClassName("owie-app")[0]).getPropertyValue(prop))) props[prop] = jsonData.current.value;
    })
    
    for (const [key, value] of Object.entries(props)) {
      style.setProperty(key, value);
    }

    document.getElementsByClassName("current-text")[0].getElementsByClassName("value-text")[0].innerText =jsonData.current.value;

    document.getElementsByClassName("voltage-text")[0].getElementsByClassName("value-text")[0].innerText = jsonData.voltage.value;

    let nCells = 0;
    for (let c of document.getElementsByClassName("battery-voltage-text")) {
      let cellVolt = jsonData.battery_cells.value[nCells];
      c.innerText = cellVolt;
      nCells++;
    }

    // NEED to calculate the percentage for progressbar
    // min:0 max:60
    let temps = 0;
    for (let c of document.querySelectorAll(".temp-item")) {
      let temp = jsonData.temperatures.value[temps];
      let percentage = 100/60*temp;
      let gaugeBar = c.querySelector('.temperature-gauge__bar');
      let lbl = c.querySelector('.value-text');
      gaugeBar.style.width = percentage + "%";
      lbl.innerText = temp;
      // Add visuals to temperatur gauges
      let severity = TEMPERATURE_SERVERITY_OFFSETS[temps] || 'good';
      if (!gaugeBar.classList.contains(severity)) {
        // clear all severity levels and add the new on
        gaugeBar.classList.remove("good", ...Object.values(TEMPERATURE_SERVERITY_OFFSETS));
        gaugeBar.classList.add(severity);
      }

      temps++;
    }

    let usage = document.getElementsByClassName("usage-statistics")[0].getElementsByClassName("usage")[0].getElementsByClassName("value-text")[0];
    let regen = document.getElementsByClassName("usage-statistics")[0].getElementsByClassName("regen")[0].getElementsByClassName("value-text")[0];

    if (usage.innerText != jsonData.usage.value) {
      usage.innerText = jsonData.usage.value;
    }
    if (regen.innerText != jsonData.regen.value) {
      regen.innerText = jsonData.regen.value;
    }

    // generate and update the stats Page!
    let statsContainer = document.querySelector('.stats.content-container .wrapper');
    let template = document.getElementById("stat-container-template");
    setTimeout(() => {
      Object.entries(jsonData).forEach((entry) => {
        const [key, item] = entry;
        if (!Array.isArray(item.value)) {
          item.value =  [item.value];
        }
        if (!statsDomWritten) {
          item.value.forEach((value, idx) => {
            let rowTpl = template.content.cloneNode(true) ;
            rowTpl.firstElementChild.classList.add(`stats-${key}_${idx}`);
            statsContainer.append(rowTpl);
          })

        }
        item.value.forEach((value, idx) => {
          let baseEl = document.getElementsByClassName(`stats-${key}_${idx}`)[0];
          baseEl.querySelector('.owie-label').innerHTML = (item.label ||key.toUpperCase()) + "&nbsp;" + ((item.value.length > 1)?idx:"");
          baseEl.querySelector('.value-text').innerHTML = value;
          baseEl.querySelector('.value-label').innerHTML = "&nbsp;" + item.unit;
        });
      });
      statsDomWritten = true;
    }, 0);
  } catch (e) {
    handleError(e);
  }
}

// startup the App and trigger metadata fetch.
// this function also handles ALL triggers and handlers.
let startup = async () => {
  // pull metadata
  await handleMetadata();

  // init the router
  Router.init();

  // handle clicking the unlock button of the bord locking feature an the home screen
  const unlockButton = document.querySelector(".unlock-button");
  unlockButton.addEventListener('click', async (e) => {
    await disarmBoard(unlockButton);
  })

  // handle clicking one of the battery reset buttons
  document.querySelectorAll(".battery-reset").forEach((btn) => {
    btn.addEventListener('click', async (e) => {
      await resetBatteryStats(btn.dataset.type);
    })
  })

  // firmware update section
  const firmwareInput = document.getElementById("fwUpdInput");
  const updateUpdateBtn = document.getElementById("fwUpdBtn");
  updateUpdateBtn.addEventListener('click', (e) => {
    firmwareInput.click();
  })

  //  setting firmware update
  firmwareInput.onchange = async (event) => {
    if (!event || !event.target || !event.target.files || event.target.files.length === 0) {
      return;
    }
    const file = event.target.files[0];
    const fileName = file.name;

    // only allow .bin || .bin.gz!
    let re = /(\.bin|\.bin\.gz)$/gm;
    if (!re.exec(fileName)) {
      handleError("error", "Only .bin or .bin.gz files are allowed!");
      return false;
    }

    updateProgress = document.querySelector(".firmware .update-upload-progress"),
    updateText = updateProgress.querySelector(".update-text"),
    updateLoader = updateProgress.querySelector(".lds-spinner");
    updateProgress.classList.remove('hidden');
    try {
      // generate formdata since direct creation in call is NOT working!!
      // This may be a minify problem!! (removing parenteses by function call);
      let fileUploadData = new FormData();
      fileUploadData.append("firmware", file);
      processUpdate = true;
      await callOwieApi("POST", "update", fileUploadData ,({loaded, total}) => {
       let fileLoaded = Math.floor((loaded / total) * 100);
       updateText.innerHTML = `File upload in progress... (${fileLoaded}%)`;
        if (loaded == total) {
          updateText.innerHTML = "Firmware Update in progress...";
        }
      });
      let countdown = 15;
      updateLoader.classList.add('hidden');
      showAlerter("success", "Update was successful!<br>OWIE is rebooting...");
      let reloadingTimeout = () => {
        if (countdown === 0) {
          updateText.innerHTML = "Reloading OWIE..."
          window.location.replace("/");
          return;
        }
        updateText.innerHTML = `IMPORTANT: keep the board powered on until Owie Wi-Fi becomes available again!<br>Autoreload after ${countdown} seconds!`;
        countdown--;
        setTimeout(reloadingTimeout, 1000);
      }
      reloadingTimeout();
    } catch (e) {
      handleError(e);
    }
  }
  // end firmware update section

  // settings WiFI update
  const wifiUpdateForm = document.getElementById("wifi-settings");
  if (wifiUpdateForm.attachEvent) {
    wifiUpdateForm.attachEvent("submit", updateWifiSettings);
  } else {
    wifiUpdateForm.addEventListener("submit", updateWifiSettings);
  }
  // end settings WiFi update

  // update Dev WiFi
  const wifiDevUpdateForm = document.getElementById("wifi-dev");
  if (wifiDevUpdateForm.attachEvent) {
    wifiDevUpdateForm.attachEvent("submit", updateWifiDevSettings);
  } else {
    wifiDevUpdateForm.addEventListener("submit", updateWifiDevSettings);
  }
  // end settings WiFi update

  // handle board arming visibility
  handleBordArmingVisibility();

  // start autoupdate timer
  let autoUpdateTimer = async () => {
    await getAutoupdate();
    setTimeout(autoUpdateTimer, updateInterval);
  }
  autoUpdateTimer();

  // hide loading div
  document.querySelector(".startup").classList.add('hide');
}

// start app
startup();