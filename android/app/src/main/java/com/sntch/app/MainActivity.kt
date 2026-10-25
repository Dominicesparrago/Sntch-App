package com.sntch.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import com.sntch.app.ui.AppContainer
import com.sntch.app.ui.SntchRoot
import com.sntch.app.ui.SntchTheme

class MainActivity : ComponentActivity() {
    private lateinit var container: AppContainer

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        container = AppContainer(applicationContext)
        setContent {
            SntchTheme {
                SntchRoot(container)
            }
        }
    }
}
