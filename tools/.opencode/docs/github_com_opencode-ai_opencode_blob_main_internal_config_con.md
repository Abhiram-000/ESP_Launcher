# opencode/internal/config/config.go at main · opencode-ai/opencode · GitHub

> Source: https://github.com/opencode-ai/opencode/blob/main/internal/config/config.go
> Cached: 2026-05-27T06:12:58.805Z

---

This repository was archived by the owner on Sep 18, 2025. It is now read-only.
  

  

  

      

        
            
  
      
    

    
    
      
        opencode-ai
    
    /
    
      [opencode](/opencode-ai/opencode)
    

    Public archive
  

        

        
            

    
      

  
                
    
Notifications
    You must be signed in to change notification settings

  

  
              
    
Fork
    1.4k

  

  
        
            
    

          Star
          12.7k

  

        
      

        

          

  

      
  
    
              
    

        Code
          

    

      
  
    
              
    

        Issues
          116

    

      
  
    
              
    

        Pull requests
          42

    

      
  
    
              
    

        Discussions
          

    

      
  
    
              
    

        Actions
          

    

      
  
    
              
    

        Projects
          

    

      
  
    
              
    

        Security and quality
          0

    

      
  
    
              
    

        Insights
          

    

          
  
      
    

Additional navigation options

  
    
                
  
    

        
    
    
    
        
          
    

        
      
        
          Code
      

  

        
    
    
    
        
          
    

        
      
        
          Issues
      

  

        
    
    
    
        
          
    

        
      
        
          Pull requests
      

  

        
    
    
    
        
          
    

        
      
        
          Discussions
      

  

        
    
    
    
        
          
    

        
      
        
          Actions
      

  

        
    
    
    
        
          
    

        
      
        
          Projects
      

  

        
    
    
    
        
          
    

        
      
        
          Security and quality
      

  

        
    
    
    
        
          
    

        
      
        
          Insights
      

  

    

      
  

  
  

    
    

    
      
    

  
  
    [](/opencode-ai/opencode)   ## FilesExpand file tree

 main## Breadcrumbs

- [opencode](/opencode-ai/opencode/tree/main)
- /[internal](/opencode-ai/opencode/tree/main/internal)
- /[config](/opencode-ai/opencode/tree/main/internal/config)

/# config.go

Copy pathBlameMore file actionsBlameMore file actions ## Latest commit

 ## History

[History](/opencode-ai/opencode/commits/main/internal/config/config.go)[](/opencode-ai/opencode/commits/main/internal/config/config.go)History980 lines (852 loc) · 28.3 KB main## Breadcrumbs

- [opencode](/opencode-ai/opencode/tree/main)
- /[internal](/opencode-ai/opencode/tree/main/internal)
- /[config](/opencode-ai/opencode/tree/main/internal/config)

/# config.go

Top## File metadata and controls

- Code
- Blame

980 lines (852 loc) · 28.3 KB[Raw](https://github.com/opencode-ai/opencode/raw/refs/heads/main/internal/config/config.go)Copy raw fileDownload raw fileOpen symbols panelEdit and raw actions123456789101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899100101102103104105106107108109110111112113114115116117118119120121122123124125126127128129130131132133134135136137138139140141142143144145146147148149150151152153154155156157158159160161162163164165166167168169170171172173174175176177178179180181182183184185186187188189190191192193194195196197198199200201202203204205206207208209210211212213214215216217218219220221222223224225226227228229230231232233234235236237238239240241242243244245246247248249250251252253254255256257258259260261262263264265266267268269270271272273274275276277278279280281282283284285286287288289290291292293294295296297298299300301302303304305306307308309310311312313314315316317318319320321322323324325326327328329330331332333334335336337338339340341342343344345346347348349350351352353354355356357358359360361362363364365366367368369370371372373374375376377378379380381382383384385386387388389390391392393394395396397398399400401402403404405406407408409410411412413414415416417418419420421422423424425426427428429430431432433434435436437438439440441442443444445446447448449450451452453454455456457458459460461462463464465466467468469470471472473474475476477478479480481482483484485486487488489490491492493494495496497498499500501502503504505506507508509510511512513514515516517518519520521522523524525526527528529530531532533534535536537538539540541542543544545546547548549550551552553554555556557558559560561562563564565566567568569570571572573574575576577578579580581582583584585586587588589590591592593594595596597598599600601602603604605606607608609610611612613614615616617618619620621622623624625626627628629630631632633634635636637638639640641642643644645646647648649650651652653654655656657658659660661662663664665666667668669670671672673674675676677678679680681682683684685686687688689690691692693694695696697698699700701702703704705706707708709710711712713714715716717718719720721722723724725726727728729730731732733734735736737738739740741742743744745746747748749750751752753754755756757758759760761762763764765766767768769770771772773774775776777778779780781782783784785786787788789790791792793794795796797798799800801802803804805806807808809810811812813814815816817818819820821822823824825826827828829830831832833834835836837838839840841842843844845846847848849850851852853854855856857858859860861862863864865866867868869870871872873874875876877878879880881882883884885886887888889890891892893894895896897898899900901902903904905906907908909910911912913914915916917918919920921922923924925926927928929930931932933934935936937938939940941942943944945946947948949950951952953954955956957958959960961962963964965966967968969970971972973974975976977978979980// Package config manages application configuration from various sources.package config
import (	"encoding/json"	"fmt"	"log/slog"	"os"	"path/filepath"	"runtime"	"strings"
	"github.com/opencode-ai/opencode/internal/llm/models"	"github.com/opencode-ai/opencode/internal/logging"	"github.com/spf13/viper")
// MCPType defines the type of MCP (Model Control Protocol) server.type MCPType string
// Supported MCP typesconst (	MCPStdio MCPType = "stdio"	MCPSse   MCPType = "sse")
// MCPServer defines the configuration for a Model Control Protocol server.type MCPServer struct {	Command string            `json:"command"`	Env     []string          `json:"env"`	Args    []string          `json:"args"`	Type    MCPType           `json:"type"`	URL     string            `json:"url"`	Headers map[string]string `json:"headers"`}
type AgentName string
const (	AgentCoder      AgentName = "coder"	AgentSummarizer AgentName = "summarizer"	AgentTask       AgentName = "task"	AgentTitle      AgentName = "title")
// Agent defines configuration for different LLM models and their token limits.type Agent struct {	Model           models.ModelID `json:"model"`	MaxTokens       int64          `json:"maxTokens"`	ReasoningEffort string         `json:"reasoningEffort"` // For openai models low,medium,heigh}
// Provider defines configuration for an LLM provider.type Provider struct {	APIKey   string `json:"apiKey"`	Disabled bool   `json:"disabled"`}
// Data defines storage configuration.type Data struct {	Directory string `json:"directory,omitempty"`}
// LSPConfig defines configuration for Language Server Protocol integration.type LSPConfig struct {	Disabled bool     `json:"enabled"`	Command  string   `json:"command"`	Args     []string `json:"args"`	Options  any      `json:"options"`}
// TUIConfig defines the configuration for the Terminal User Interface.type TUIConfig struct {	Theme string `json:"theme,omitempty"`}
// ShellConfig defines the configuration for the shell used by the bash tool.type ShellConfig struct {	Path string   `json:"path,omitempty"`	Args []string `json:"args,omitempty"`}
// Config is the main configuration structure for the application.type Config struct {	Data         Data                              `json:"data"`	WorkingDir   string                            `json:"wd,omitempty"`	MCPServers   map[string]MCPServer              `json:"mcpServers,omitempty"`	Providers    map[models.ModelProvider]Provider `json:"providers,omitempty"`	LSP          map[string]LSPConfig              `json:"lsp,omitempty"`	Agents       map[AgentName]Agent               `json:"agents,omitempty"`	Debug        bool                              `json:"debug,omitempty"`	DebugLSP     bool                              `json:"debugLSP,omitempty"`	ContextPaths []string                          `json:"contextPaths,omitempty"`	TUI          TUIConfig                         `json:"tui"`	Shell        ShellConfig                       `json:"shell,omitempty"`	AutoCompact  bool                              `json:"autoCompact,omitempty"`}
// Application constantsconst (	defaultDataDirectory = ".opencode"	defaultLogLevel      = "info"	appName              = "opencode"
	MaxTokensFallbackDefault = 4096)
var defaultContextPaths = []string{	".github/copilot-instructions.md",	".cursorrules",	".cursor/rules/",	"CLAUDE.md",	"CLAUDE.local.md",	"opencode.md",	"opencode.local.md",	"OpenCode.md",	"OpenCode.local.md",	"OPENCODE.md",	"OPENCODE.local.md",}
// Global configuration instancevar cfg *Config
// Load initializes the configuration from environment variables and config files.// If debug is true, debug mode is enabled and log level is set to debug.// It returns an error if configuration loading fails.func Load(workingDir string, debug bool) (*Config, error) {	if cfg != nil {		return cfg, nil	}
	cfg = &Config{		WorkingDir: workingDir,		MCPServers: make(map[string]MCPServer),		Providers:  make(map[models.ModelProvider]Provider),		LSP:        make(map[string]LSPConfig),	}
	configureViper()	setDefaults(debug)
	// Read global config	if err := readConfig(viper.ReadInConfig()); err != nil {		return cfg, err	}
	// Load and merge local config	mergeLocalConfig(workingDir)
	setProviderDefaults()
	// Apply configuration to the struct	if err := viper.Unmarshal(cfg); err != nil {		return cfg, fmt.Errorf("failed to unmarshal config: %w", err)	}
	applyDefaultValues()	defaultLevel := slog.LevelInfo	if cfg.Debug {		defaultLevel = slog.LevelDebug	}	if os.Getenv("OPENCODE_DEV_DEBUG") == "true" {		loggingFile := fmt.Sprintf("%s/%s", cfg.Data.Directory, "debug.log")		messagesPath := fmt.Sprintf("%s/%s", cfg.Data.Directory, "messages")
		// if file does not exist create it		if _, err := os.Stat(loggingFile); os.IsNotExist(err) {			if err := os.MkdirAll(cfg.Data.Directory, 0o755); err != nil {				return cfg, fmt.Errorf("failed to create directory: %w", err)			}			if _, err := os.Create(loggingFile); err != nil {				return cfg, fmt.Errorf("failed to create log file: %w", err)			}		}
		if _, err := os.Stat(messagesPath); os.IsNotExist(err) {			if err := os.MkdirAll(messagesPath, 0o756); err != nil {				return cfg, fmt.Errorf("failed to create directory: %w", err)			}		}		logging.MessageDir = messagesPath
		sloggingFileWriter, err := os.OpenFile(loggingFile, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o666)		if err != nil {			return cfg, fmt.Errorf("failed to open log file: %w", err)		}		// Configure logger		logger := slog.New(slog.NewTextHandler(sloggingFileWriter, &slog.HandlerOptions{			Level: defaultLevel,		}))		slog.SetDefault(logger)	} else {		// Configure logger		logger := slog.New(slog.NewTextHandler(logging.NewWriter(), &slog.HandlerOptions{			Level: defaultLevel,		}))		slog.SetDefault(logger)	}
	// Validate configuration	if err := Validate(); err != nil {		return cfg, fmt.Errorf("config validation failed: %w", err)	}
	if cfg.Agents == nil {		cfg.Agents = make(map[AgentName]Agent)	}
	// Override the max tokens for title agent	cfg.Agents[AgentTitle] = Agent{		Model:     cfg.Agents[AgentTitle].Model,		MaxTokens: 80,	}	return cfg, nil}
// configureViper sets up viper&#039;s configuration paths and environment variables.func configureViper() {	viper.SetConfigName(fmt.Sprintf(".%s", appName))	viper.SetConfigType("json")	viper.AddConfigPath("$HOME")	viper.AddConfigPath(fmt.Sprintf("$XDG_CONFIG_HOME/%s", appName))	viper.AddConfigPath(fmt.Sprintf("$HOME/.config/%s", appName))	viper.SetEnvPrefix(strings.ToUpper(appName))	viper.AutomaticEnv()}
// setDefaults configures default values for configuration options.func setDefaults(debug bool) {	viper.SetDefault("data.directory", defaultDataDirectory)	viper.SetDefault("contextPaths", defaultContextPaths)	viper.SetDefault("tui.theme", "opencode")	viper.SetDefault("autoCompact", true)
	// Set default shell from environment or fallback to /bin/bash	shellPath := os.Getenv("SHELL")	if shellPath == "" {		shellPath = "/bin/bash"	}	viper.SetDefault("shell.path", shellPath)	viper.SetDefault("shell.args", []string{"-l"})
	if debug {		viper.SetDefault("debug", true)		viper.Set("log.level", "debug")	} else {		viper.SetDefault("debug", false)		viper.SetDefault("log.level", defaultLogLevel)	}}
// setProviderDefaults configures LLM provider defaults based on provider provided by// environment variables and configuration file.func setProviderDefaults() {	// Set all API keys we can find in the environment	// Note: Viper does not default if the json apiKey is ""	if apiKey := os.Getenv("ANTHROPIC_API_KEY"); apiKey != "" {		viper.SetDefault("providers.anthropic.apiKey", apiKey)	}	if apiKey := os.Getenv("OPENAI_API_KEY"); apiKey != "" {		viper.SetDefault("providers.openai.apiKey", apiKey)	}	if apiKey := os.Getenv("GEMINI_API_KEY"); apiKey != "" {		viper.SetDefault("providers.gemini.apiKey", apiKey)	}	if apiKey := os.Getenv("GROQ_API_KEY"); apiKey != "" {		viper.SetDefault("providers.groq.apiKey", apiKey)	}	if apiKey := os.Getenv("OPENROUTER_API_KEY"); apiKey != "" {		viper.SetDefault("providers.openrouter.apiKey", apiKey)	}	if apiKey := os.Getenv("XAI_API_KEY"); apiKey != "" {		viper.SetDefault("providers.xai.apiKey", apiKey)	}	if apiKey := os.Getenv("AZURE_OPENAI_ENDPOINT"); apiKey != "" {		// api-key may be empty when using Entra ID credentials – that&#039;s okay		viper.SetDefault("providers.azure.apiKey", os.Getenv("AZURE_OPENAI_API_KEY"))	}	if apiKey, err := LoadGitHubToken(); err == nil && apiKey != "" {		viper.SetDefault("providers.copilot.apiKey", apiKey)		if viper.GetString("providers.copilot.apiKey") == "" {			viper.Set("providers.copilot.apiKey", apiKey)		}	}
	// Use this order to set the default models	// 1. Copilot	// 2. Anthropic	// 3. OpenAI	// 4. Google Gemini	// 5. Groq	// 6. OpenRouter	// 7. AWS Bedrock	// 8. Azure	// 9. Google Cloud VertexAI
	// copilot configuration	if key := viper.GetString("providers.copilot.apiKey"); strings.TrimSpace(key) != "" {		viper.SetDefault("agents.coder.model", models.CopilotGPT4o)		viper.SetDefault("agents.summarizer.model", models.CopilotGPT4o)		viper.SetDefault("agents.task.model", models.CopilotGPT4o)		viper.

... [Content truncated]