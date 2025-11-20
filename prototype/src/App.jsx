import { useMemo, useState } from 'react'
import previewAlien from '../assets/alien-flying-enemy/spritesheet.png'
import previewBunny from '../assets/sunny-bunny/Spritesheets/sunny-bunny-idle.png'
import previewMushroom from '../assets/sunny-mushroom/spritesheets/sunny-mushroom-walk.png'
import previewDetective from '../assets/cyberpunk-detective/spritesheets/gun-walk.png'
import previewShooter from '../assets/top-down-shooter-enemies/spritesheets/enemy-01.png'
import previewDragon from '../assets/sunny-dragon/spritesheets/sunny-dragon-fly.png'
import previewNightmare from '../assets/Nightmare-Files/Spritesheets/idle.png'
import previewDemon from '../assets/demon-Files/Spritesheets/demon-attack.png'
import previewSpaceMarine from '../assets/space-marine/Spritesheet.png'
import previewFroggy from '../assets/sunny-froggy/Spritesheets/sunny-froggy-walk.png'
import previewRobot from '../assets/top-down-dungeon-enemy-robot/Spritesheets/robot-walk-front.png'
import previewWolf from '../assets/wolf-runing-cycle/spritesheets/wolf-runing-cycle.png'
import previewGhost from '../assets/Ghost-Files/Spritesheets/ghost-Idle.png'

const folderTree = [
  {
    name: 'alien-flying-enemy',
    path: 'alien-flying-enemy',
    children: [
      { name: 'sprites', path: 'alien-flying-enemy/sprites' },
      { name: 'spritesheet.png', path: 'alien-flying-enemy/spritesheet.png' },
    ],
  },
  { name: 'sunny-bunny', path: 'sunny-bunny', children: [{ name: 'Spritesheets', path: 'sunny-bunny/Spritesheets' }] },
  { name: 'sunny-mushroom', path: 'sunny-mushroom', children: [{ name: 'spritesheets', path: 'sunny-mushroom/spritesheets' }] },
  { name: 'cyberpunk-detective', path: 'cyberpunk-detective', children: [{ name: 'spritesheets', path: 'cyberpunk-detective/spritesheets' }] },
  { name: 'top-down-shooter-enemies', path: 'top-down-shooter-enemies', children: [{ name: 'spritesheets', path: 'top-down-shooter-enemies/spritesheets' }] },
  { name: 'sunny-dragon', path: 'sunny-dragon', children: [{ name: 'spritesheets', path: 'sunny-dragon/spritesheets' }] },
  { name: 'Nightmare-Files', path: 'Nightmare-Files', children: [{ name: 'Spritesheets', path: 'Nightmare-Files/Spritesheets' }] },
  { name: 'demon-Files', path: 'demon-Files', children: [{ name: 'Spritesheets', path: 'demon-Files/Spritesheets' }] },
  { name: 'space-marine', path: 'space-marine', children: [{ name: 'Sprites', path: 'space-marine/Sprites' }] },
  { name: 'sunny-froggy', path: 'sunny-froggy', children: [{ name: 'Spritesheets', path: 'sunny-froggy/Spritesheets' }] },
  { name: 'top-down-dungeon-enemy-robot', path: 'top-down-dungeon-enemy-robot', children: [{ name: 'Spritesheets', path: 'top-down-dungeon-enemy-robot/Spritesheets' }] },
  { name: 'wolf-runing-cycle', path: 'wolf-runing-cycle', children: [{ name: 'spritesheets', path: 'wolf-runing-cycle/spritesheets' }] },
  { name: 'Ghost-Files', path: 'Ghost-Files', children: [{ name: 'Spritesheets', path: 'Ghost-Files/Spritesheets' }] },
]

const typeFilterOptions = [
  { key: '2D', label: '2D' },
  { key: '3D', label: '3D' },
  { key: 'Audio', label: 'Audio' },
  { key: 'Shader', label: 'Shader' },
  { key: 'Font', label: 'Font' },
]

const mockAssets = [
  {
    id: 'AST-2049',
    name: 'Nebula Cargo Pod',
    type: '3D',
    preview: previewAlien,
    category: '3D Model',
    extension: 'fbx',
    path: 'World/Orbital Freight/Cargo Pods',
    folderPath: 'alien-flying-enemy',
    size: '92 MB',
    modified: '2025-03-04 11:20',
    status: 'Ready for Review',
    shading: 'linear-gradient(135deg, #ffbb88, #ff6f91, #ff3f7f)',
    tags: ['Sci-Fi', 'Logistics', 'Hard Surface'],
    vertices: '2.1M',
    faces: '1.0M',
  },
  {
    id: 'AST-2173',
    name: 'Aurora Botanical Deck',
    type: '2D',
    preview: previewBunny,
    category: 'Texture Set',
    extension: 'png',
    path: 'World/Terraform Vistas/Botanical Deck',
    folderPath: 'sunny-bunny/Spritesheets',
    size: '1.7 GB',
    modified: '2025-03-03 08:40',
    status: 'In QA Queue',
    shading: 'linear-gradient(135deg, #a9ff6f, #36e4a6, #00c6b8)',
    tags: ['Organic', 'Tileable', 'Greenhouse'],
    vertices: '350K',
    faces: '220K',
  },
  {
    id: 'AST-1888',
    name: 'Driftshore Sand Ripple',
    type: 'Shader',
    preview: previewMushroom,
    category: 'Surface Shader',
    extension: 'shader',
    path: 'Terrain/Procedural/Coastal/Driftshore',
    folderPath: 'sunny-mushroom/spritesheets',
    size: '48 MB',
    modified: '2025-03-01 14:08',
    status: 'Verified',
    shading: 'linear-gradient(135deg, #ffd166, #ffa41b, #ff7c1f)',
    tags: ['Terrain', 'Procedural', 'Stylized'],
    vertices: '120K',
    faces: '95K',
  },
  {
    id: 'AST-2310',
    name: 'Iridescent Alloy Panel',
    type: '3D',
    preview: previewDetective,
    category: '3D Model',
    extension: 'glb',
    path: 'World/Starship Interior/Panels/Iridescent',
    folderPath: 'cyberpunk-detective/spritesheets',
    size: '63 MB',
    modified: '2025-03-04 05:50',
    status: 'Updated',
    shading: 'linear-gradient(135deg, #8fd3ff, #4ea7ff, #2f6bff)',
    tags: ['Hard Surface', 'Modular', 'Panel'],
    vertices: '1.2M',
    faces: '630K',
  },
  {
    id: 'AST-2095',
    name: 'Lumen Street Kit',
    type: '3D',
    preview: previewShooter,
    category: 'Kitbash Set',
    extension: 'zip',
    path: 'City/Neo Lumen/Streets/Kit',
    folderPath: 'top-down-shooter-enemies/spritesheets',
    size: '2.4 GB',
    modified: '2025-03-04 13:15',
    status: 'Processing',
    shading: 'linear-gradient(135deg, #ffb2f5, #ff4fb6, #f72585)',
    tags: ['Urban', 'Modular', 'Night'],
    vertices: '3.8M',
    faces: '2.7M',
  },
  {
    id: 'AST-1870',
    name: 'Cryostone Cliff',
    type: '2D',
    preview: previewDragon,
    category: 'Scan',
    extension: 'exr',
    path: 'Environment/Glacial Shelf/Cryostone/Scan A',
    folderPath: 'sunny-dragon/spritesheets',
    size: '4.1 GB',
    modified: '2025-03-02 18:10',
    status: 'Needs Cleanup',
    shading: 'linear-gradient(135deg, #8be7ff, #4fe1ff, #24b4ff)',
    tags: ['Environment', 'Scan', 'Cliff'],
    vertices: '4.6M',
    faces: '3.3M',
  },
  {
    id: 'AST-2403',
    name: 'Sonar Bloom Pad',
    type: 'Audio',
    preview: previewNightmare,
    category: 'Audio Stem',
    extension: 'wav',
    path: 'Audio/Atmospheres/Sonar Bloom',
    folderPath: 'Nightmare-Files/Spritesheets',
    size: '210 MB',
    modified: '2025-02-27 09:45',
    status: 'Ready for Review',
    shading: 'linear-gradient(135deg, #6fefef, #28c0ff, #006dff)',
    tags: ['Pad', 'Ambient'],
    vertices: '45K',
    faces: '28K',
  },
  {
    id: 'AST-2444',
    name: 'Auric Surface Shader',
    type: 'Shader',
    preview: previewDemon,
    category: 'Material Graph',
    extension: 'shader',
    path: 'Shaders/Surface/Auric/Revision_04',
    folderPath: 'demon-Files/Spritesheets',
    size: '36 MB',
    modified: '2025-03-05 07:02',
    status: 'Updated',
    shading: 'linear-gradient(135deg, #ffd166, #ff5e62, #ff9966)',
    tags: ['Surface', 'Energy'],
    vertices: '640K',
    faces: '410K',
  },
  {
    id: 'AST-2501',
    name: 'Luna Glyph Serif',
    type: 'Font',
    preview: previewSpaceMarine,
    category: 'Typeface',
    extension: 'otf',
    path: 'Fonts/Display/LunaGlyphSerif',
    folderPath: 'space-marine/Sprites',
    size: '12 MB',
    modified: '2025-02-22 15:30',
    status: 'Verified',
    shading: 'linear-gradient(135deg, #ffe8f2, #ff8cb7, #ff2d78)',
    tags: ['Typography', 'Serif'],
    vertices: '80K',
    faces: '52K',
  },
  {
    id: 'AST-2520',
    name: 'Sunrise Froggy Rig',
    type: '3D',
    preview: previewFroggy,
    category: 'Character',
    extension: 'blend',
    path: 'Creatures/Sunny/Froggy/Rig',
    folderPath: 'sunny-froggy/Spritesheets',
    size: '540 MB',
    modified: '2025-03-05 19:12',
    status: 'Ready for Review',
    shading: 'linear-gradient(135deg, #9cffc7, #4fecb5, #17c2d0)',
    tags: ['Rigged', 'Bouncy', 'NPC'],
    vertices: '890K',
    faces: '610K',
  },
  {
    id: 'AST-2531',
    name: 'Dungeon Robot Walk',
    type: '2D',
    preview: previewRobot,
    category: 'Sprite Sheet',
    extension: 'png',
    path: 'Props/Dungeon/Robots/Walker',
    folderPath: 'top-down-dungeon-enemy-robot/Spritesheets',
    size: '32 MB',
    modified: '2025-02-28 11:00',
    status: 'Updated',
    shading: 'linear-gradient(135deg, #9ea7ff, #6c7eff, #4256f3)',
    tags: ['Robot', 'Dungeon'],
    vertices: '120K',
    faces: '80K',
  },
  {
    id: 'AST-2544',
    name: 'Night Wolf Cycle',
    type: '3D',
    preview: previewWolf,
    category: 'Animation',
    extension: 'fbx',
    path: 'Animations/Wildlife/Wolf/Night Cycle',
    folderPath: 'wolf-runing-cycle/spritesheets',
    size: '210 MB',
    modified: '2025-03-01 07:55',
    status: 'Processing',
    shading: 'linear-gradient(135deg, #aabfff, #6f8cff, #2e3a73)',
    tags: ['Animation', 'Quadruped'],
    vertices: '1.6M',
    faces: '1.1M',
  },
  {
    id: 'AST-2552',
    name: 'Specter Idle FX',
    type: 'Shader',
    preview: previewGhost,
    category: 'VFX Sprite',
    extension: 'png',
    path: 'VFX/Spectral/GhostIdle',
    folderPath: 'Ghost-Files/Spritesheets',
    size: '18 MB',
    modified: '2025-02-20 16:44',
    status: 'Verified',
    shading: 'linear-gradient(135deg, #d8f2ff, #92d4ff, #46a6ff)',
    tags: ['VFX', 'Ghost'],
    vertices: '65K',
    faces: '40K',
  },
]

const getPreviewStyle = (asset) => ({
  backgroundImage: asset.shading,
  backgroundSize: 'cover',
  backgroundPosition: 'center',
})

const statusTone = {
  Verified: 'text-tropic-700 bg-tropic-50 border border-tropic-200',
  'Ready for Review': 'text-mango-700 bg-mango-50 border border-mango-200',
  Updated: 'text-tide-700 bg-tide-50 border border-tide-200',
  Processing: 'text-coral-700 bg-coral-50 border border-coral-200',
  'Needs Cleanup': 'text-coral-700 bg-coral-50 border border-coral-200',
  'In QA Queue': 'text-tide-700 bg-tide-50 border border-tide-200',
}

function AssetCard({ asset, isActive, onSelect }) {
  return (
    <button
      type="button"
      onClick={() => onSelect(asset)}
      aria-label={`Open ${asset.name}`}
      className={`group relative block max-w-[260px] overflow-hidden rounded-[18px] border border-white/30 bg-white transition duration-300 hover:-translate-y-1 focus-visible:outline focus-visible:outline-2 focus-visible:outline-offset-4 focus-visible:outline-tide-300 ${
        isActive ? 'ring-4 ring-tide-200' : ''
      }`}
    >
      <div className="relative h-[240px] w-full overflow-hidden rounded-[14px]">
        {asset.preview ? (
          <img
            src={asset.preview}
            alt={asset.name}
            className="absolute inset-0 h-full w-full object-cover"
            style={{ imageRendering: 'pixelated' }}
          />
        ) : (
          <div className="absolute inset-0" style={getPreviewStyle(asset)} />
        )}
        <div className="absolute inset-0 bg-gradient-to-b from-transparent via-transparent to-black/30" />
        <div className="absolute inset-0 bg-gradient-to-tr from-white/35 via-transparent to-white/10 opacity-0 transition duration-300 group-hover:opacity-100" />
        <span className="pointer-events-none absolute bottom-5 left-5 translate-y-3 font-display text-2xl text-white drop-shadow-[0_8px_18px_rgba(0,0,0,0.45)] opacity-0 transition duration-300 group-hover:translate-y-0 group-hover:opacity-100">
          {asset.name}
        </span>
      </div>
    </button>
  )
}

function PathBreadcrumbs({ path, onPathSelect, activePath, enableHighlight }) {
  const segments = path.split('/')
  return (
    <div className="mt-3 flex flex-wrap gap-2">
      {segments.map((segment, index) => {
        const crumbPath = segments.slice(0, index + 1).join('/')
        const isActive = enableHighlight && activePath === crumbPath
        return (
          <button
            key={crumbPath}
            type="button"
            onClick={() => onPathSelect(crumbPath)}
            className={`rounded-full border px-3 py-1 text-xs font-medium transition ${
              isActive
                ? 'border-tide-400 bg-tide-100 text-midnight'
                : 'border-white/60 bg-white text-midnight/70 hover:border-tide-200'
            }`}
          >
            {segment}
          </button>
        )
      })}
    </div>
  )
}

function DetailPanel({ asset, onPathSelect, activePath, pathFilterEnabled }) {
  if (!asset) return null

  const infoFields = [
    { label: 'Extension', value: asset.extension?.toUpperCase() },
    { label: 'Type', value: asset.type },
    { label: 'Size', value: asset.size },
    { label: 'Modified', value: asset.modified },
    { label: 'Vertices', value: asset.vertices },
    { label: 'Faces', value: asset.faces },
  ].filter((field) => field.value)

  return (
    <aside className="max-h-[75vh] overflow-y-auto border border-white/40 bg-gradient-to-b from-white via-tide-50 to-tropic-50 p-6 pr-8">
      <div className="space-y-6">
        <div className="relative aspect-square overflow-hidden rounded-[18px] border border-white bg-white/90">
          {asset.preview ? (
            <img
              src={asset.preview}
              alt={asset.name}
              className="absolute inset-0 h-full w-full object-cover"
              style={{ imageRendering: 'pixelated' }}
            />
          ) : (
            <div className="absolute inset-0" style={getPreviewStyle(asset)} />
          )}
        </div>

        <div>
          <p className="text-xs uppercase tracking-[0.3em] text-tide-500">Path</p>
          <PathBreadcrumbs
            path={asset.path}
            onPathSelect={onPathSelect}
            activePath={activePath}
            enableHighlight={pathFilterEnabled}
          />
        </div>

        <div className="flex flex-col gap-2 text-sm">
          {infoFields.map((field) => (
            <div key={field.label} className="flex items-baseline justify-between gap-4 border-b border-white/40 pb-2 last:border-b-0">
              <p className="text-xs uppercase tracking-[0.2em] text-tide-500">{field.label}</p>
              <p className="text-base font-medium text-midnight">{field.value}</p>
            </div>
          ))}
        </div>

      </div>
    </aside>
  )
}

function FolderNode({ node, depth, expandedFolders, toggleFolder, checkedFolders, toggleCheck }) {
  const isExpanded = expandedFolders.has(node.path)
  const isChecked = checkedFolders.has(node.path)
  const hasChildren = node.children && node.children.length > 0

  return (
    <div className="py-1">
      <div className="flex items-center gap-2">
        {hasChildren ? (
          <button
            type="button"
            onClick={() => toggleFolder(node.path)}
            className="text-sm text-tide-500"
          >
            {isExpanded ? '−' : '+'}
          </button>
        ) : (
          <span className="w-3" />
        )}
        <label className="flex flex-1 items-center gap-2 text-sm">
          <input
            type="checkbox"
            checked={isChecked}
            onChange={() => toggleCheck(node.path)}
          />
          <span className="text-midnight/80">
            {node.name}
          </span>
        </label>
      </div>
      {hasChildren && isExpanded && (
        <div className="ml-1 border-l border-white/30 pl-2">
          {node.children.map((child) => (
            <FolderNode
              key={child.path}
              node={child}
              depth={depth + 1}
              expandedFolders={expandedFolders}
              toggleFolder={toggleFolder}
              checkedFolders={checkedFolders}
              toggleCheck={toggleCheck}
            />
          ))}
        </div>
      )}
    </div>
  )
}

function FolderExplorer({ tree, expandedFolders, toggleFolder, checkedFolders, toggleCheck }) {
  return (
    <div className="border border-white/40 bg-white/70 p-4">
      <p className="text-xs uppercase tracking-[0.3em] text-tide-500">assets folders</p>
      <div className="mt-3 max-h-[40vh] overflow-y-auto pr-2">
        {tree.map((node) => (
          <FolderNode
            key={node.path}
            node={node}
            depth={0}
            expandedFolders={expandedFolders}
            toggleFolder={toggleFolder}
            checkedFolders={checkedFolders}
            toggleCheck={toggleCheck}
          />
        ))}
      </div>
    </div>
  )
}

function App() {
  const [query, setQuery] = useState('')
  const [selectedAsset, setSelectedAsset] = useState(mockAssets[0])
  const [pathFilter, setPathFilter] = useState(null)
  const [pathFilterActive, setPathFilterActive] = useState(false)
  const [typeFilters, setTypeFilters] = useState(() =>
    typeFilterOptions.reduce((acc, option) => ({ ...acc, [option.key]: false }), {})
  )
  const [searchFocused, setSearchFocused] = useState(false)
  const [expandedFolders, setExpandedFolders] = useState(() => new Set())
  const [checkedFolders, setCheckedFolders] = useState(() => new Set())

  const activeTypeFilters = useMemo(
    () => Object.entries(typeFilters).filter(([, selected]) => selected).map(([key]) => key),
    [typeFilters],
  )
  const activeFolderFilters = useMemo(() => Array.from(checkedFolders), [checkedFolders])

  const filteredAssets = useMemo(() => {
    const queryLower = query.trim().toLowerCase()

    return mockAssets.filter((asset) => {
      const matchesQuery = queryLower
        ? `${asset.name} ${asset.id} ${asset.tags.join(' ')} ${asset.path}`.toLowerCase().includes(queryLower)
        : true

      const matchesType = activeTypeFilters.length === 0 || activeTypeFilters.includes(asset.type)
      const matchesPath = !pathFilterActive || !pathFilter
        ? true
        : asset.path.toLowerCase().startsWith(pathFilter.toLowerCase())

      const matchesFolder =
        activeFolderFilters.length === 0 ||
        activeFolderFilters.some((folder) => asset.folderPath?.toLowerCase().startsWith(folder.toLowerCase()))

      return matchesQuery && matchesType && matchesPath && matchesFolder
    })
  }, [query, activeTypeFilters, pathFilter, pathFilterActive, activeFolderFilters])

  const toggleTypeFilter = (key) => {
    setTypeFilters((prev) => ({ ...prev, [key]: !prev[key] }))
  }

  const handlePathToggle = () => {
    if (!pathFilter) return
    setPathFilterActive((prev) => !prev)
  }

  const handlePathSelect = (path) => {
    setPathFilter(path)
    setPathFilterActive(true)
  }

  const clearPathFilter = () => {
    setPathFilter(null)
    setPathFilterActive(false)
  }

  const toggleFolderExpand = (path) => {
    setExpandedFolders((prev) => {
      const next = new Set(prev)
      if (next.has(path)) {
        next.delete(path)
      }
      else {
        next.add(path)
      }
      return next
    })
  }

  const toggleFolderCheck = (path) => {
    setCheckedFolders((prev) => {
      const next = new Set(prev)
      if (next.has(path)) {
        next.delete(path)
      }
      else {
        next.add(path)
      }
      return next
    })
  }

  return (
    <div className="min-h-screen bg-[#fdfaf2] pb-16">
      <div className="flex min-h-screen flex-col gap-10 px-6 pt-10 sm:px-10">
        <div className="grid grow gap-10 lg:grid-cols-[minmax(0,1fr)_20%]">
          <div className="flex flex-col gap-8">
            <header className="flex flex-col gap-6 items-center">
              <div className="flex justify-center w-full">
                <div
                  className={`flex w-full max-w-3xl flex-wrap items-center gap-4 rounded-[32px] px-5 py-4 shadow-sm ${
                    searchFocused ? 'bg-[#ffd25a]' : 'bg-[#f0ede4]'
                  }`}
                >
                  <span className={searchFocused ? 'text-midnight' : 'text-tide-500'}>
                    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" role="presentation">
                      <path
                        d="M11 4a7 7 0 015.6 11.2l3.1 3.1-1.4 1.4-3.1-3.1A7 7 0 1111 4z"
                        stroke={searchFocused ? '#1b1230' : '#28c0ff'}
                        strokeWidth="1.5"
                        strokeLinecap="round"
                        strokeLinejoin="round"
                      />
                    </svg>
                  </span>
                  <input
                    type="text"
                    value={query}
                    onChange={(event) => setQuery(event.target.value)}
                    onFocus={() => setSearchFocused(true)}
                    onBlur={() => setSearchFocused(false)}
                    className="flex-1 border-none bg-transparent text-lg text-midnight focus:outline-none"
                  />
                  <kbd className="rounded-full px-3 py-1 text-xs font-semibold text-tide-500">⌘K</kbd>
                </div>
              </div>

              <div className="flex w-full max-w-3xl flex-wrap justify-center gap-3">
                {typeFilterOptions.map((option) => {
                  const isActive = typeFilters[option.key]
                  return (
                    <button
                      key={option.key}
                      type="button"
                      onClick={() => toggleTypeFilter(option.key)}
                      className={`rounded-full border px-4 py-1 text-sm font-semibold transition ${
                        isActive ? 'border-tide-400 bg-tide-100 text-midnight' : 'border-transparent bg-white/60 text-midnight/60'
                      }`}
                    >
                      {option.label}
                    </button>
                  )
                })}

                {pathFilter && (
                  <div className="flex items-center gap-2">
                    <button
                      type="button"
                      onClick={handlePathToggle}
                      title={pathFilter}
                      className={`rounded-full border px-4 py-1 text-sm font-semibold transition ${
                        pathFilterActive ? 'border-tropic-400 bg-tropic-100 text-midnight' : 'border-transparent bg-white/60 text-midnight/60'
                      }`}
                    >
                      Path
                    </button>
                    <button
                      type="button"
                      onClick={clearPathFilter}
                      className="rounded-full border border-transparent bg-transparent text-xs text-midnight/50 underline"
                    >
                      Clear
                    </button>
                  </div>
                )}
              </div>
            </header>

            <section className="space-y-6 max-h-[75vh] overflow-y-auto pr-0 lg:pr-4">
              <div className="mb-5 flex flex-wrap items-center justify-between gap-3 text-sm text-midnight/70">
                <p>
                  Showing <span className="font-semibold text-midnight">{filteredAssets.length}</span> of {mockAssets.length} indexed assets
                </p>
                {pathFilterActive && pathFilter && (
                  <p className="text-xs text-midnight/60">Path filter: {pathFilter}</p>
                )}
              </div>

              {filteredAssets.length === 0 ? (
                <div className="flex min-h-[240px] items-center justify-center rounded-[16px] border border-dashed border-white/60 bg-white/70 text-tide-500">
                  Nothing matches that vibe.
                </div>
              ) : (
                <div
                  className="grid gap-4"
                  style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(240px, 1fr))' }}
                >
                  {filteredAssets.map((asset) => (
                    <AssetCard
                      key={asset.id}
                      asset={asset}
                      isActive={selectedAsset?.id === asset.id}
                      onSelect={setSelectedAsset}
                    />
                  ))}
                </div>
              )}
            </section>
          </div>

          <div className="flex flex-col gap-4 pr-0 lg:pr-4">
            <DetailPanel
              asset={selectedAsset}
              onPathSelect={handlePathSelect}
              activePath={pathFilter}
              pathFilterEnabled={pathFilterActive}
            />
            <FolderExplorer
              tree={folderTree}
              expandedFolders={expandedFolders}
              toggleFolder={toggleFolderExpand}
              checkedFolders={checkedFolders}
              toggleCheck={toggleFolderCheck}
            />
          </div>
        </div>
      </div>
    </div>
  )
}

export default App
