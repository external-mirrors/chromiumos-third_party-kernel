# ChromeOS Kernel Scripts

For details on how to use these scripts, please also see:
https://www.chromium.org/chromium-os/developer-library/guides/kernel/kernel-configuration/

## `cros_make_fit.py`

The `cros_make_fit.py` script builds a FIT image containing a kernel,
a set of devicetree files (DTBs/DTBOs), and a list of configurations.

### DTB Config YAML File

The `--dtb-config` argument specifies a YAML file that describes the
DTB/DTBO configurations for different models.
This provides a structured way to manage device configurations.

The YAML file is a dictionary where each key is a model name (e.g.,
`skywalker`).
A special `DEFAULT` key can be used to define common configurations
that are shared across all models.

Each model configuration is a dictionary with the following keys:

- **`skus`**: A list of SKU configurations for the model.
  If the `--chromeos-config` argument is provided,
  this `skus` section is ignored,
  and the SKU information is sourced directly from the
  specified ChromeOS config file
  (`${SYSROOT}/usr/share/chromeos-config/yaml/config.yaml` in CrOS SDK) instead.
- **`dtb`**: A dictionary of DTB files and their matching attributes.
- **`dtbo`**: A dictionary of DTBO files and their matching attributes.

The following is an example:

```yaml
DEFAULT:
  dtbo:
    common-overlay.dtbo: {}  # Matches any SKU

skywalker:
  skus:
    - sku: 0
      fw_config: 0x00000000
    - sku: 1
      fw_config: 0x00000010
  dtb:
    skywalker-rev0.dtb:
      rev:  # Matches rev 0
        max: 0
    skywalker.dtb:
      rev:  # Matches rev >= 1
        min: 1
  dtbo:
    skywalker-sku8.dtbo:
      sku: [8]
    skywalker-rt5682i.dtbo:
      fw_config:  # Matches codec RT5682I by "fw_config & 0x30 == 0x10"
        mask: 0x00000030
        value: 0x00000010
```

#### DTB/DTBO Matching Attributes

Under the `dtb` and `dtbo` keys, each entry consists of a DTB/DTBO
filename and a dictionary of attributes.
These attributes determine which device configuration the file applies
to.

The following matching attributes are available:

- **`rev`**: Matches the board revision. It can have `min` (defaults to 0
  if omitted) and `max` values to specify a range.
  ```yaml
  rev:
    min: 1  # Matches revision 1 and newer
    max: 5  # Matches revision 5 and older
  ```
- **`sku`**: A list of SKU IDs that this DTB/DTBO applies to.
  ```yaml
  sku: [0, 1, 2] # Matches if SKU is 0, 1, or 2
  ```
- **`fw_config`**: Matches the firmware configuration.
  It has a `mask` and a `value`.
  The DTB/DTBO is a match if `(fw_config & mask) == value`.
  ```yaml
  fw_config:
    mask: 0x30
    value: 0x10
  ```

All attributes are optional.
Only specified attributes are used for matching.
An empty dictionary (`{}`) for a DTB/DTBO file means
it matches all configurations for that model.

### Generation of FIT Configuration Nodes

For each model, the `cros_make_fit.py` script generates
FIT configuration nodes by merging `<rev, sku>` pairs that
share the same DTB and DTBO files.
This means a single configuration node in the FIT image can represent
multiple `<revision, sku>` combinations if they utilize an identical
set of device tree binaries.

Each generated configuration node contains a `description` and a
`compatible` property.
The `compatible` property is a string list,
where each string identifies a specific `<model, revision, sku>`
combination that maps to this configuration node.
The bootloader uses these `compatible` strings to select the
appropriate configuration node.

Each individual compatible string within the list follows this format:
`google,<model>[-rev<rev>]-sku<sku>`

- **`<model>`**: The model name (e.g., `skywalker`).
- **`[-rev<rev>]`**: An optional board revision.
  This part is omitted for the latest revision of a given model.
  The script automatically determines a "latest revision" for each model
  based on the `rev` attributes in the YAML file.
  This is a conceptual revision number that is numerically
  greater than any specific revision defined.
  For example, if the highest revision specified is `min: 5`
  (without `max`), the latest revision is considered `5`.
  If the highest revision specified is `min: 5, max: 7`,
  the latest revision is considered `8`.
  This creates a fallback compatible string (e.g., `google,skywalker-sku0`)
  that bootloaders can use for future board revisions without requiring a
  kernel update.
- **`<sku>`**: The SKU ID.

For example, if `skywalker` model with `rev 0/sku 0` and `rev 0/sku 1`
both use the same DTB/DTBO set, a single FIT configuration node will be
generated, with the compatible string being
`"google,skywalker-rev0-sku0", "google,skywalker-rev0-sku1"`.
If `skywalker` model with `rev 5/sku 0` is the latest revision, its
compatible string will be `"google,skywalker-sku0"`.