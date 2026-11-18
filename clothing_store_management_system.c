// Clothing Store Management System (Programming Fundamentals project)
// Updated with Master Admin hierarchy, dynamic admin persistence (admins.txt),
// private login, account unlocking, and clean loop/block structure.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

#define MAX_ADMINS       20     /* maximum dynamic admin accounts */
#define MAX_ATTEMPTS     3
#define MAX_CART         10
#define NUM_SIZES        4
#define LOW_STOCK_LIMIT  5      /* total units below this => low-stock alert */
#define LOYAL_RATE       0.08f
#define DATA_FILE        "store.txt"
#define SALES_FILE       "sales.txt"
#define ADMIN_FILE       "admins.txt"

const char *SIZE_NAMES[NUM_SIZES] = {"S", "M", "L", "XL"};

struct Clothing {
    int   id;
    char  name[50], category[30], season[20];
    float price;
    int   sizeStock[NUM_SIZES];   /* [0]=S [1]=M [2]=L [3]=XL */
    int   sold;                   /* units sold (for best-seller report) */
};

struct CartLine {
    int idx;    /* index into inv[] */
    int size;   /* 0..3 */
    int qty;    /* > 0 */
};

struct Admin {
    char name[20];
    int  pin;
    int  locked;
    int  isMaster;                /* 1 = Master Admin, 0 = Sub-Admin */
};

/* Global dynamic inventory */
struct Clothing *inv = NULL;
int   count = 0, cap = 0;
float totalSales = 0.0f;

/* Global dynamic admin list */
struct Admin admins[MAX_ADMINS];
int   adminCount = 0;
int   currentAdmin = -1;          /* index of the admin who is logged in */

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
void save(void);
void saveSales(void);
void saveAdmins(void);
void browse(void);   /* used by update/delete/restock/checkout before it is defined */

void quitProgram(void) {          /* used if input stream ends (EOF) */
    printf("\nInput closed. Saving and exiting...\n");
    save();
    saveAdmins();
    free(inv);
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Safe input helpers                                                  */
/* ------------------------------------------------------------------ */
int readLine(char *buf, int n) {
    if (!fgets(buf, n, stdin)) return 0;
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    else { int c; while ((c = getchar()) != '\n' && c != EOF); }  /* discard overflow */
    return 1;
}

/* Reads an integer in [lo, hi]; keeps asking until valid. */
int readInt(const char *prompt, int lo, int hi) {
    char buf[64], *end;
    for (;;) {
        printf("%s", prompt);
        if (!readLine(buf, sizeof buf)) quitProgram();
        long v = strtol(buf, &end, 10);
        if (end == buf) { printf("That is not a number. Please try again.\n"); continue; }
        while (isspace((unsigned char)*end)) end++;
        if (*end != '\0') { printf("Please type a whole number only (no letters or decimals).\n"); continue; }
        if (v < lo || v > hi) { printf("Please enter a number between %d and %d.\n", lo, hi); continue; }
        return (int)v;
    }
}

/* Reads a float in [lo, hi]; keeps asking until valid. */
float readFloat(const char *prompt, float lo, float hi) {
    char buf[64], *end;
    for (;;) {
        printf("%s", prompt);
        if (!readLine(buf, sizeof buf)) quitProgram();
        double v = strtod(buf, &end);
        if (end == buf) { printf("That is not a number. Please try again.\n"); continue; }
        while (isspace((unsigned char)*end)) end++;
        if (*end != '\0') { printf("That is not a number. Please try again.\n"); continue; }
        if (v < lo || v > hi) { printf("Please enter a value between %.2f and %.2f.\n", lo, hi); continue; }
        return (float)v;
    }
}

/* Reads a non-empty text; spaces become '_' so the file format stays simple. */
void readWord(const char *prompt, char *dest, int size) {
    char buf[128];
    for (;;) {
        printf("%s", prompt);
        if (!readLine(buf, sizeof buf)) quitProgram();
        char *s = buf;
        while (isspace((unsigned char)*s)) s++;
        int len = (int)strlen(s);
        while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
        if (len == 0) { printf("This field cannot be empty.\n"); continue; }
        int i;
        for (i = 0; s[i]; i++) if (isspace((unsigned char)s[i])) s[i] = '_';
        strncpy(dest, s, size - 1);
        dest[size - 1] = '\0';
        return;
    }
}

/* Portable case-insensitive helpers (strcasecmp is not available everywhere) */
int ciCompare(const char *a, const char *b) {
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d) return d;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int ciEqual(const char *a, const char *b) { return ciCompare(a, b) == 0; }

int ciContains(const char *text, const char *pat) {
    size_t n = strlen(text), m = strlen(pat);
    if (m == 0 || m > n) return m == 0;
    size_t i;
    for (i = 0; i + m <= n; i++) {
        size_t j = 0;
        while (j < m && tolower((unsigned char)text[i + j]) == tolower((unsigned char)pat[j])) j++;
        if (j == m) return 1;
    }
    return 0;
}

/* Generic menu: returns a validated choice in 1..n */
int menu(const char *title, const char *opts[], int n) {
    printf("\n=== %s ===\n", title);
    int i;
    for (i = 0; i < n; i++) printf("  %d. %s\n", i + 1, opts[i]);
    return readInt("Enter your choice: ", 1, n);
}

/* ------------------------------------------------------------------ */
/* Inventory helpers                                                   */
/* ------------------------------------------------------------------ */

/* Grows the inventory safely. Returns 1 on success, 0 on failure. */
int ensureCap(void) {
    if (count < cap) return 1;
    int newCap = cap ? cap * 2 : 5;
    struct Clothing *tmp = (struct Clothing *)realloc(inv, (size_t)newCap * sizeof *inv);
    if (!tmp) {                       /* keep old block valid on failure */
        printf("Memory allocation failed!\n");
        return 0;
    }
    inv = tmp;
    cap = newCap;
    return 1;
}

int findById(int id) {
    int i;
    for (i = 0; i < count; i++) if (inv[i].id == id) return i;
    return -1;
}

int totalStock(const struct Clothing *c) {
    int t = 0;
    int s;
    for (s = 0; s < NUM_SIZES; s++) t += c->sizeStock[s];
    return t;
}

/* Shows EVERY size together with its quantity, so the user can see which size has
   how many pieces. Returns the chosen size index (0..3). */
int readSize(const struct Clothing *c) {
    printf("\nSizes available for %s:\n", c->name);
    int s;
    for (s = 0; s < NUM_SIZES; s++) {
        printf("  %d. Size %-2s : %d piece(s)", s + 1, SIZE_NAMES[s], c->sizeStock[s]);
        if (c->sizeStock[s] == 0) printf("  (sold out)");
        printf("\n");
    }
    return readInt("Choose a size number: ", 1, NUM_SIZES) - 1;
}

void todayStr(char out[11]) {
    time_t t = time(NULL);
    strftime(out, 11, "%Y-%m-%d", localtime(&t));
}

/* ------------------------------------------------------------------ */
/* Admin security & management (Master Admin + Sub-Admins)             */
/* ------------------------------------------------------------------ */

int findAdminByName(const char *name) {
    int i;
    for (i = 0; i < adminCount; i++) {
        if (ciEqual(admins[i].name, name)) return i;
    }
    return -1;
}

void saveAdmins(void) {
    FILE *f = fopen(ADMIN_FILE, "w");
    if (!f) { printf("Could not open %s for writing!\n", ADMIN_FILE); return; }
    fprintf(f, "%d\n", adminCount);
    int i;
    for (i = 0; i < adminCount; i++) {
        fprintf(f, "%s %d %d %d\n", admins[i].name, admins[i].pin,
                admins[i].locked, admins[i].isMaster);
    }
    fclose(f);
}

void loadAdmins(void) {
    FILE *f = fopen(ADMIN_FILE, "r");
    if (!f) {
        /* First-time setup: initialize default Master Admin */
        strcpy(admins[0].name, "Ali");
        admins[0].pin = 1234;
        admins[0].locked = 0;
        admins[0].isMaster = 1;
        adminCount = 1;
        saveAdmins();
        return;
    }
    int c;
    if (fscanf(f, "%d", &c) != 1 || c <= 0 || c > MAX_ADMINS) {
        fclose(f);
        strcpy(admins[0].name, "Ali");
        admins[0].pin = 1234;
        admins[0].locked = 0;
        admins[0].isMaster = 1;
        adminCount = 1;
        saveAdmins();
        return;
    }
    adminCount = 0;
    int i;
    for (i = 0; i < c; i++) {
        struct Admin a;
        if (fscanf(f, "%19s %d %d %d", a.name, &a.pin, &a.locked, &a.isMaster) == 4) {
            admins[adminCount++] = a;
        }
    }
    fclose(f);
    if (adminCount == 0) {
        strcpy(admins[0].name, "Ali");
        admins[0].pin = 1234;
        admins[0].locked = 0;
        admins[0].isMaster = 1;
        adminCount = 1;
        saveAdmins();
    }
}

/* Private Login: Prompts for username directly instead of listing all admin names */
int login(void) {
    char uname[20];
    printf("\n--- ADMIN LOGIN ---\n");
    readWord("Enter admin username (or 'cancel' to go back): ", uname, sizeof uname);
    if (ciEqual(uname, "cancel")) return 0;

    int who = findAdminByName(uname);
    if (who == -1) {
        printf("Invalid username or account does not exist.\n");
        return 0;
    }

    if (admins[who].locked) {
        if (admins[who].isMaster) {
            printf("Master Admin account '%s' is locked. Please restart the program.\n", admins[who].name);
        } else {
            printf("Account '%s' is locked due to multiple failed PIN attempts.\n", admins[who].name);
            printf("Please ask the Master Admin to unlock your account.\n");
        }
        return 0;
    }

    char buf[64], *end;
    int att;
    for (att = MAX_ATTEMPTS; att > 0; ) {
        printf("Enter 4-digit PIN for %s: ", admins[who].name);
        if (!readLine(buf, sizeof buf)) quitProgram();
        long p = strtol(buf, &end, 10);
        if (end != buf && *end == '\0' && p == admins[who].pin) {
            currentAdmin = who;
            printf("Login successful. Welcome, %s (%s)!\n",
                   admins[who].name, admins[who].isMaster ? "Master Admin" : "Sub-Admin");
            return 1;
        }
        att--;
        if (att > 0) printf("Wrong PIN. You have %d attempt(s) left.\n", att);
    }

    admins[who].locked = 1;
    saveAdmins();
    printf("Too many wrong attempts. %s's account is now LOCKED.\n", admins[who].name);
    return 0;
}

/* Master Admin Exclusives */
void addSubAdmin(void) {
    if (adminCount >= MAX_ADMINS) {
        printf("Maximum number of admin accounts (%d) reached.\n", MAX_ADMINS);
        return;
    }
    printf("\n--- REGISTER NEW SUB-ADMIN ---\n");
    char name[20];
    readWord("Enter new sub-admin username: ", name, sizeof name);
    if (ciEqual(name, "cancel") || findAdminByName(name) != -1) {
        printf("Username is invalid or already exists.\n");
        return;
    }
    int pin = readInt("Set a 4-digit PIN (1000 - 9999): ", 1000, 9999);
    strcpy(admins[adminCount].name, name);
    admins[adminCount].pin = pin;
    admins[adminCount].locked = 0;
    admins[adminCount].isMaster = 0;
    adminCount++;
    saveAdmins();
    printf("Sub-Admin '%s' created and saved successfully!\n", name);
}

void viewAndUnlockAdmins(void) {
    printf("\n--- REGISTERED ADMIN ACCOUNTS ---\n");
    printf("%-18s %-14s %-10s\n", "Username", "Role", "Status");
    int i;
    for (i = 0; i < adminCount; i++) {
        printf("%-18s %-14s %-10s\n",
               admins[i].name,
               admins[i].isMaster ? "Master Admin" : "Sub-Admin",
               admins[i].locked ? "LOCKED" : "Active");
    }

    printf("\n1. Unlock a locked account\n2. Remove a sub-admin\n3. Change own PIN\n4. Back\n");
    int ch = readInt("Choose an action (1-4): ", 1, 4);
    if (ch == 4) return;

    if (ch == 3) {
        int newPin = readInt("Enter your new 4-digit PIN (1000 - 9999): ", 1000, 9999);
        admins[currentAdmin].pin = newPin;
        saveAdmins();
        printf("Your PIN has been updated!\n");
        return;
    }

    char target[20];
    readWord("Enter the username of the sub-admin: ", target, sizeof target);
    int idx = findAdminByName(target);
    if (idx == -1) {
        printf("Admin user not found.\n");
        return;
    }
    if (admins[idx].isMaster) {
        printf("Cannot modify or delete the Master Admin account from here.\n");
        return;
    }

    if (ch == 1) {
        if (!admins[idx].locked) {
            printf("'%s' is already active (not locked).\n", admins[idx].name);
        } else {
            admins[idx].locked = 0;
            saveAdmins();
            printf("Account '%s' has been UNLOCKED!\n", admins[idx].name);
        }
    } else if (ch == 2) {
        int j;
        for (j = idx; j < adminCount - 1; j++) admins[j] = admins[j + 1];
        adminCount--;
        saveAdmins();
        printf("Sub-Admin '%s' has been removed.\n", target);
    }
}

/* ------------------------------------------------------------------ */
/* Admin operations                                                    */
/* ------------------------------------------------------------------ */

void addItem(void) {
    if (!ensureCap()) return;
    printf("\n--- ADD NEW ITEM ---\n");
    int id = readInt("Item ID (positive number, 0 = cancel): ", 0, INT_MAX);
    if (id == 0) { printf("Cancelled.\n"); return; }
    if (findById(id) != -1) { printf("Duplicate ID! An item with ID %d already exists. Item not added.\n", id); return; }

    struct Clothing *c = &inv[count];
    memset(c, 0, sizeof *c);
    c->id = id;
    readWord("Item name: ", c->name, sizeof c->name);
    printf("Category:\n  1. Formal\n  2. Casual\n");
    int cat = readInt("Choose category (1-2): ", 1, 2);
    strcpy(c->category, cat == 1 ? "Formal" : "Casual");
    const char *seasonNames[] = {"Spring", "Summer", "Autumn", "Winter"};
    printf("Season:\n  1. Spring\n  2. Summer\n  3. Autumn\n  4. Winter\n");
    int sea = readInt("Choose season (1-4): ", 1, 4);
    strcpy(c->season, seasonNames[sea - 1]);
    c->price = readFloat("Price: ", 0.01f, 1000000.0f);
    printf("How many pieces do you have in each size?\n");
    int s;
    for (s = 0; s < NUM_SIZES; s++) {
        char p[40];
        sprintf(p, "  Size %-2s: ", SIZE_NAMES[s]);
        c->sizeStock[s] = readInt(p, 0, 100000);
    }
    c->sold = 0;
    count++;
    printf("Added successfully: %s (%s, %s) $%.2f  Stock: S:%d M:%d L:%d XL:%d\n", c->name, c->category, c->season,
           c->price, c->sizeStock[0], c->sizeStock[1], c->sizeStock[2], c->sizeStock[3]);
}

/* Pass-by-reference update: the pointer lets us modify the caller's record */
void updateItem(struct Clothing *it) {
    printf("Current price: $%.2f   Current stock: S:%d M:%d L:%d XL:%d\n", it->price,
           it->sizeStock[0], it->sizeStock[1], it->sizeStock[2], it->sizeStock[3]);
    it->price = readFloat("New price: $", 0.01f, 1000000.0f);
    int s;
    for (s = 0; s < NUM_SIZES; s++) {
        char p[40];
        sprintf(p, "New stock for size %-2s: ", SIZE_NAMES[s]);
        it->sizeStock[s] = readInt(p, 0, 100000);
    }
    printf("Updated via pass-by-reference!\n");
}

void updateMenu(void) {
    if (!count) { printf("Inventory empty.\n"); return; }
    printf("\n--- UPDATE AN ITEM ---\n");
    browse();
    int id = readInt("\nEnter the ID of the item to update (0 = cancel): ", 0, INT_MAX);
    if (id == 0) { printf("Cancelled.\n"); return; }
    int i = findById(id);
    if (i == -1) { printf("No item has that ID.\n"); return; }
    printf("Selected: %s\n", inv[i].name);
    updateItem(&inv[i]);
}

void deleteItem(void) {
    if (!admins[currentAdmin].isMaster) {
        printf("Access Denied: Only the Master Admin can permanently delete items.\n");
        return;
    }
    if (!count) { printf("Inventory empty.\n"); return; }
    printf("\n--- DELETE AN ITEM ---\n");
    browse();
    int id = readInt("\nEnter the ID of the item to delete (0 = cancel): ", 0, INT_MAX);
    if (id == 0) { printf("Cancelled.\n"); return; }
    int i = findById(id);
    if (i == -1) { printf("No item has that ID.\n"); return; }
    printf("Selected: %s\n", inv[i].name);
    if (readInt("Are you sure you want to delete it? (1 = Yes, 0 = No): ", 0, 1) == 0) {
        printf("Nothing was deleted.\n");
        return;
    }
    int j;
    for (j = i; j < count - 1; j++) inv[j] = inv[j + 1];
    count--;
    printf("Deleted!\n");
}

/* Separate restock operation */
void restock(void) {
    if (!count) { printf("Inventory empty.\n"); return; }
    printf("\n--- RESTOCK AN ITEM ---\n");
    browse();
    int id = readInt("\nEnter the ID of the item to restock (0 = cancel): ", 0, INT_MAX);
    if (id == 0) { printf("Cancelled.\n"); return; }
    int i = findById(id);
    if (i == -1) { printf("No item has that ID.\n"); return; }
    printf("Selected: %s\n", inv[i].name);
    int s = readSize(&inv[i]);
    int q = readInt("How many pieces are you adding? ", 1, 100000);
    inv[i].sizeStock[s] += q;
    printf("Restocked %s, size %s. New stock in size %s: %d piece(s).\n", inv[i].name, SIZE_NAMES[s], SIZE_NAMES[s], inv[i].sizeStock[s]);
}

void browse(void) {
    if (!count) { printf("Inventory empty.\n"); return; }
    printf("\n%-5s %-15s %-10s %-8s %-8s %s\n", "ID", "Name", "Category", "Season", "Price", "Stock by size");
    int i;
    for (i = 0; i < count; i++) {
        printf("%-5d %-15s %-10s %-8s $%-7.2f S:%d M:%d L:%d XL:%d\n",
               inv[i].id, inv[i].name, inv[i].category, inv[i].season, inv[i].price,
               inv[i].sizeStock[0], inv[i].sizeStock[1], inv[i].sizeStock[2], inv[i].sizeStock[3]);
    }
}

/* Low-stock alert + restock reminder (Cleaned block formatting) */
void lowStock(void) {
    int found = 0;
    printf("\n--- Low Stock Alerts / Restock Reminders ---\n");
    int i;
    for (i = 0; i < count; i++) {
        if (totalStock(&inv[i]) < LOW_STOCK_LIMIT) {
            printf("LOW: '%s' (ID: %d) has only %d units. Restock needed!\n",
                   inv[i].name, inv[i].id, totalStock(&inv[i]));
            found = 1;
        }
        int s;
        for (s = 0; s < NUM_SIZES; s++) {
            if (inv[i].sizeStock[s] == 0) {
                printf("REMINDER: '%s' size %s is sold out - consider restocking.\n",
                       inv[i].name, SIZE_NAMES[s]);
                found = 1;
            }
        }
    }
    if (!found) printf("All items are sufficiently stocked.\n");
}

/* ------------------------------------------------------------------ */
/* Sorting (selection sort)                                            */
/* ------------------------------------------------------------------ */

/* key: 1=name, 2=id, 3=price, 4=total stock  (all ascending) */
int isBefore(const struct Clothing *a, const struct Clothing *b, int key) {
    switch (key) {
        case 1:  return ciCompare(a->name, b->name) < 0;
        case 2:  return a->id < b->id;
        case 3:  return a->price < b->price;
        default: return totalStock(a) < totalStock(b);
    }
}

void sortInv(int key) {
    int i;
    for (i = 0; i < count - 1; i++) {
        int m = i;
        int j;
        for (j = i + 1; j < count; j++) {
            if (isBefore(&inv[j], &inv[m], key)) m = j;
        }
        if (m != i) { struct Clothing t = inv[i]; inv[i] = inv[m]; inv[m] = t; }
    }
}

void sortMenu(void) {
    const char *opts[] = {"By Name", "By ID", "By Price", "By Stock"};
    int k = menu("SORT ITEMS", opts, 4);
    sortInv(k);
    browse();
}

/* ------------------------------------------------------------------ */
/* Searching                                                           */
/* ------------------------------------------------------------------ */
/* Recursive binary search (inventory must be sorted by ID) */
int binSearch(int id, int low, int high) {
    if (low > high) return -1;
    int mid = low + (high - low) / 2;
    if (inv[mid].id == id) return mid;
    return inv[mid].id > id ? binSearch(id, low, mid - 1) : binSearch(id, mid + 1, high);
}

void searchById(void) {
    if (!count) { printf("Inventory empty.\n"); return; }
    sortInv(2);                                   /* binary search needs ID order */
    int id = readInt("Enter the item ID to search for: ", 1, INT_MAX);
    int idx = binSearch(id, 0, count - 1);
    if (idx == -1) { printf("No item found with that ID.\n"); return; }
    printf("Found: ID %d, %s, %s, $%.2f, stock S:%d M:%d L:%d XL:%d\n", inv[idx].id, inv[idx].name,
           inv[idx].category, inv[idx].price, inv[idx].sizeStock[0], inv[idx].sizeStock[1],
           inv[idx].sizeStock[2], inv[idx].sizeStock[3]);
}

void searchByName(void) {
    char q[50];
    int found = 0;
    readWord("Type part of the item name (capital or small letters both work): ", q, sizeof q);
    int i;
    for (i = 0; i < count; i++) {
        if (ciContains(inv[i].name, q)) {
            printf("Found: ID %d, %s, $%.2f, stock S:%d M:%d L:%d XL:%d\n", inv[i].id, inv[i].name, inv[i].price,
                   inv[i].sizeStock[0], inv[i].sizeStock[1], inv[i].sizeStock[2], inv[i].sizeStock[3]);
            found = 1;
        }
    }
    if (!found) printf("No item matches that name.\n");
}

/* ------------------------------------------------------------------ */
/* Billing                                                             */
/* ------------------------------------------------------------------ */
/* Seasonal rate depends on category (switch + nested if) */
float seasonRate(int season, const char *category) {
    float rate = 0.0f;
    switch (season) {
        case 1: if (ciEqual(category, "Casual")) rate = 0.05f; break;  /* Spring */
        case 2: if (ciEqual(category, "Casual")) rate = 0.15f; break;  /* Summer */
        case 3: if (ciEqual(category, "Formal")) rate = 0.10f; break;  /* Autumn */
        case 4: if (ciEqual(category, "Formal")) rate = 0.20f; break;  /* Winter */
    }
    return rate;
}

float bulkRate(int qty) {
    if (qty >= 5) return 0.10f;
    else if (qty >= 3) return 0.05f;
    return 0.0f;
}

/* Recursive subtotal (no discounts) */
float recSubtotal(const struct CartLine cart[], int n, int idx) {
    if (idx >= n) return 0.0f;
    return inv[cart[idx].idx].price * cart[idx].qty + recSubtotal(cart, n, idx + 1);
}

/* Recursive net total: discounts applied sequentially per line */
float recNetTotal(const struct CartLine cart[], int n, int idx, int season, int loyal) {
    if (idx >= n) return 0.0f;
    const struct Clothing *c = &inv[cart[idx].idx];
    float amt = c->price * cart[idx].qty;
    amt *= (1.0f - seasonRate(season, c->category));   /* 1) seasonal */
    amt *= (1.0f - bulkRate(cart[idx].qty));           /* 2) bulk     */
    if (loyal) amt *= (1.0f - LOYAL_RATE);             /* 3) loyalty  */
    return amt + recNetTotal(cart, n, idx + 1, season, loyal);
}

void checkout(void) {
    if (!count) { printf("Inventory empty.\n"); return; }
    struct CartLine cart[MAX_CART];
    int n = 0;

    printf("\n--- NEW SALE ---\n");
    browse();
    int want = readInt("\nHow many different items does the customer want? (1-10): ", 1, MAX_CART);

    int e;
    for (e = 0; e < want; e++) {
        printf("\nItem %d of %d\n", e + 1, want);
        int id = readInt("Enter item ID (0 = cancel the sale): ", 0, INT_MAX);
        if (id == 0) { printf("Checkout cancelled. Nothing was changed.\n"); return; }
        int f = findById(id);
        if (f == -1) { printf("No item has that ID. Please try again.\n"); e--; continue; }
        printf("Selected: %s  ($%.2f)\n", inv[f].name, inv[f].price);

        int size = readSize(&inv[f]);      /* shows every size with its quantity */

        /* Find if the same product+size is already in the cart */
        int existing = -1;
        int k;
        for (k = 0; k < n; k++) {
            if (cart[k].idx == f && cart[k].size == size) { existing = k; break; }
        }
        int already = (existing == -1) ? 0 : cart[existing].qty;

        /* Pieces still available = stock - already in cart. */
        int avail = inv[f].sizeStock[size] - already;
        if (avail <= 0) {
            printf("Sorry, no pieces left in size %s (stock: %d, already in your cart: %d). Please choose another size or item.\n",
                   SIZE_NAMES[size], inv[f].sizeStock[size], already);
            e--; continue;
        }
        char qp[60];
        sprintf(qp, "How many pieces of size %s? (1 to %d): ", SIZE_NAMES[size], avail);
        int qty = readInt(qp, 1, avail);

        if (existing != -1) cart[existing].qty += qty;
        else { cart[n].idx = f; cart[n].size = size; cart[n].qty = qty; n++; }
        printf("Added to cart: %d x %s, size %s.\n", qty, inv[f].name, SIZE_NAMES[size]);
    }

    printf("\nSeason:\n  1. Spring\n  2. Summer\n  3. Autumn\n  4. Winter\n");
    int season = readInt("Choose the current season (1-4): ", 1, 4);
    int loyal  = readInt("Is the customer a loyal member? (1 = Yes, 0 = No): ", 0, 1);

    float sub   = recSubtotal(cart, n, 0);
    float total = recNetTotal(cart, n, 0, season, loyal);
    if (total < 0) total = 0;
    float disc  = sub - total;

    printf("\n=============== BILL ===============\n");
    int k;
    for (k = 0; k < n; k++) {
        printf("%-15s size %-2s x%-3d $%.2f\n", inv[cart[k].idx].name, SIZE_NAMES[cart[k].size],
               cart[k].qty, inv[cart[k].idx].price * cart[k].qty);
    }
    printf("------------------------------------\n");
    printf("Subtotal:      $%.2f\n", sub);
    printf("Discount:      $%.2f\n", disc);
    printf("TOTAL TO PAY:  $%.2f\n", total);
    printf("====================================\n");

    if (readInt("Confirm this purchase? (1 = Yes, 0 = No): ", 0, 1) == 0) {
        printf("Sale cancelled. Nothing was changed.\n");
        return;
    }

    /* Deduct stock and update sales (only after the customer confirmed) */
    for (k = 0; k < n; k++) {
        inv[cart[k].idx].sizeStock[cart[k].size] -= cart[k].qty;
        inv[cart[k].idx].sold += cart[k].qty;
    }
    totalSales += total;
    saveSales();
    printf("[OK] Checkout complete. Stock updated. Thank you!\n");
}

/* ------------------------------------------------------------------ */
/* Return and exchange                                                 */
/* ------------------------------------------------------------------ */
void returnItem(void) {
    if (!count) { printf("Inventory empty.\n"); return; }
    printf("\n--- RETURN AN ITEM ---\n");
    int id = readInt("Enter the ID of the item being returned (0 = cancel): ", 0, INT_MAX);
    if (id == 0) { printf("Cancelled.\n"); return; }
    int i = findById(id);
    if (i == -1) { printf("No item has that ID.\n"); return; }
    printf("Selected: %s  ($%.2f)\n", inv[i].name, inv[i].price);
    if (inv[i].sold == 0) { printf("No pieces of this item have been sold, so nothing can be returned.\n"); return; }
    printf("(%d piece(s) of this item have been sold so far.)\n", inv[i].sold);
    int s = readSize(&inv[i]);
    int q = readInt("How many pieces are being returned? ", 1, inv[i].sold);

    float refund = inv[i].price * q;          /* refunded at list price */
    inv[i].sizeStock[s] += q;
    inv[i].sold -= q;
    totalSales -= refund;
    if (totalSales < 0) totalSales = 0;
    saveSales();
    printf("Return accepted. Refund: $%.2f. Size %s now has %d piece(s) in stock.\n", refund, SIZE_NAMES[s], inv[i].sizeStock[s]);
}

void exchangeItem(void) {
    if (!count) { printf("Inventory empty.\n"); return; }
    printf("\n--- EXCHANGE AN ITEM ---\n");
    int id1 = readInt("Enter the ID of the item being brought back (0 = cancel): ", 0, INT_MAX);
    if (id1 == 0) { printf("Cancelled.\n"); return; }
    int a = findById(id1);
    if (a == -1) { printf("No item has that ID.\n"); return; }
    printf("Selected: %s  ($%.2f)\n", inv[a].name, inv[a].price);
    if (inv[a].sold == 0) { printf("No pieces of this item have been sold, so nothing can be exchanged.\n"); return; }
    printf("Which size is being brought back?");
    int s1 = readSize(&inv[a]);
    int q  = readInt("How many pieces are being exchanged? ", 1, inv[a].sold);

    printf("\nNow choose the NEW item the customer wants.\n");
    browse();
    int id2 = readInt("\nEnter the ID of the new item (0 = cancel): ", 0, INT_MAX);
    if (id2 == 0) { printf("Cancelled.\n"); return; }
    int b = findById(id2);
    if (b == -1) { printf("New item not found.\n"); return; }
    printf("Selected: %s  ($%.2f)\n", inv[b].name, inv[b].price);
    printf("Which size does the customer want?");
    int s2 = readSize(&inv[b]);

    if (a == b && s1 == s2) { printf("Same item and size - nothing to exchange.\n"); return; }
    if (q > inv[b].sizeStock[s2]) {
        printf("Exchange denied: only %d piece(s) of '%s' in size %s are in stock.\n", inv[b].sizeStock[s2], inv[b].name, SIZE_NAMES[s2]);
        return;
    }

    inv[a].sizeStock[s1] += q;  inv[a].sold -= q;
    inv[b].sizeStock[s2] -= q;  inv[b].sold += q;

    float diff = (inv[b].price - inv[a].price) * q;
    totalSales += diff;
    if (totalSales < 0) totalSales = 0;
    saveSales();

    if (diff > 0)      printf("Exchange done. Customer pays extra: $%.2f\n", diff);
    else if (diff < 0) printf("Exchange done. Refund to customer: $%.2f\n", -diff);
    else               printf("Exchange done. No price difference.\n");
}

/* ------------------------------------------------------------------ */
/* Reports (Cleaned block formatting)                                  */
/* ------------------------------------------------------------------ */
void reports(void) {
    char today[11];
    todayStr(today);
    printf("\n--- REPORTS ---\nDaily Sales Total (%s): $%.2f\n", today, totalSales);

    /* Best-selling: sort a COPY by units sold (descending) */
    printf("\nBest-selling items:\n");
    if (count > 0) {
        struct Clothing *copy = (struct Clothing *)malloc((size_t)count * sizeof *copy);
        if (!copy) {
            printf("Memory allocation failed!\n");
        } else {
            memcpy(copy, inv, (size_t)count * sizeof *copy);
            int i;
            for (i = 0; i < count - 1; i++) {
                int m = i;
                int j;
                for (j = i + 1; j < count; j++) {
                    if (copy[j].sold > copy[m].sold) m = j;
                }
                if (m != i) { struct Clothing t = copy[i]; copy[i] = copy[m]; copy[m] = t; }
            }
            int shown = 0;
            for (i = 0; i < count && i < 3; i++) {
                if (copy[i].sold > 0) {
                    printf("%d. %s (ID %d) - %d sold\n", ++shown, copy[i].name, copy[i].id, copy[i].sold);
                }
            }
            if (!shown) printf("No sales yet.\n");
            free(copy);
        }
    } else {
        printf("Inventory empty.\n");
    }

    printf("\nOut-of-stock list:\n");
    int any = 0;
    int i;
    for (i = 0; i < count; i++) {
        int s;
        for (s = 0; s < NUM_SIZES; s++) {
            if (inv[i].sizeStock[s] == 0) {
                printf("- %s (ID %d) size %s out of stock\n", inv[i].name, inv[i].id, SIZE_NAMES[s]);
                any = 1;
            }
        }
    }
    if (!any) printf("Nothing is out of stock.\n");
}

/* ------------------------------------------------------------------ */
/* File handling                                                       */
/* ------------------------------------------------------------------ */
void saveSales(void) {
    FILE *f = fopen(SALES_FILE, "w");
    if (!f) return;
    char today[11];
    todayStr(today);
    fprintf(f, "%s %.2f\n", today, totalSales);
    fclose(f);
}

/* Restores today's sales; a new day starts again from 0 */
void loadSales(void) {
    FILE *f = fopen(SALES_FILE, "r");
    if (!f) return;
    char date[32], today[11];
    float v;
    todayStr(today);
    if (fscanf(f, "%31s %f", date, &v) == 2 && strcmp(date, today) == 0 && v >= 0) totalSales = v;
    fclose(f);
}

void save(void) {
    FILE *f = fopen(DATA_FILE, "w");
    if (!f) { printf("Could not open %s for writing!\n", DATA_FILE); return; }
    fprintf(f, "%d\n", count);
    int i;
    for (i = 0; i < count; i++) {
        fprintf(f, "%d %s %s %s %.2f %d %d %d %d %d\n", inv[i].id, inv[i].name, inv[i].category,
                inv[i].season, inv[i].price, inv[i].sizeStock[0], inv[i].sizeStock[1],
                inv[i].sizeStock[2], inv[i].sizeStock[3], inv[i].sold);
    }
    fclose(f);
    saveSales();
}

void load(void) {
    FILE *f = fopen(DATA_FILE, "r");
    if (!f) { printf("No saved data found. Starting with an empty inventory.\n"); return; }
    int c;
    if (fscanf(f, "%d", &c) != 1 || c < 0) {
        printf("Data file is corrupt. Starting with an empty inventory.\n");
        fclose(f);
        return;
    }
    int i;
    for (i = 0; i < c; i++) {
        struct Clothing t;
        memset(&t, 0, sizeof t);
        int ok = fscanf(f, "%d %49s %29s %19s %f %d %d %d %d %d", &t.id, t.name, t.category, t.season,
                        &t.price, &t.sizeStock[0], &t.sizeStock[1], &t.sizeStock[2], &t.sizeStock[3],
                        &t.sold) == 10;
        if (ok) {
            ok = t.id > 0 && t.price >= 0 && t.sold >= 0 && findById(t.id) == -1;
            int s;
            for (s = 0; s < NUM_SIZES; s++) if (t.sizeStock[s] < 0) ok = 0;
        }
        if (!ok) { printf("Corrupt record #%d found. Loading stopped.\n", i + 1); break; }
        if (!ensureCap()) break;
        inv[count++] = t;
    }
    fclose(f);
    printf("Loaded %d item(s).\n", count);
}

/* ------------------------------------------------------------------ */
/* Portals & Main                                                      */
/* ------------------------------------------------------------------ */
void adminPortal(void) {
    char title[70];
    sprintf(title, "ADMIN MENU - %s (%s)",
            admins[currentAdmin].name,
            admins[currentAdmin].isMaster ? "Master Admin" : "Sub-Admin");

    int low = 0;
    int i;
    for (i = 0; i < count; i++) if (totalStock(&inv[i]) < LOW_STOCK_LIMIT) low++;
    if (low > 0) printf("Heads up: %d item(s) are low on stock. Choose option 5 to see them.\n", low);

    if (admins[currentAdmin].isMaster) {
        const char *mOpts[] = {
            "Add a new item",
            "Update price / stock of an item",
            "Delete an item (Master Only)",
            "Restock an item (add pieces to a size)",
            "View items and low stock alerts",
            "Sort items",
            "Reports (sales, best sellers, out of stock)",
            "Register a new Sub-Admin (Master Only)",
            "Manage / Unlock Admin Accounts (Master Only)",
            "Save data now",
            "Log out"
        };
        int ach;
        while ((ach = menu(title, mOpts, 11)) != 11) {
            switch (ach) {
                case 1:  addItem();                    break;
                case 2:  updateMenu();                 break;
                case 3:  deleteItem();                 break;
                case 4:  restock();                    break;
                case 5:  browse(); lowStock();         break;
                case 6:  sortMenu();                   break;
                case 7:  reports();                    break;
                case 8:  addSubAdmin();                break;
                case 9:  viewAndUnlockAdmins();        break;
                case 10: save(); printf("Data saved.\n"); break;
            }
        }
    } else {
        const char *sOpts[] = {
            "Add a new item",
            "Update price / stock of an item",
            "Restock an item (add pieces to a size)",
            "View items and low stock alerts",
            "Sort items",
            "Reports (sales, best sellers, out of stock)",
            "Change my PIN",
            "Save data now",
            "Log out"
        };
        int ach;
        while ((ach = menu(title, sOpts, 9)) != 9) {
            switch (ach) {
                case 1: addItem();                    break;
                case 2: updateMenu();                 break;
                case 3: restock();                    break;
                case 4: browse(); lowStock();         break;
                case 5: sortMenu();                   break;
                case 6: reports();                    break;
                case 7: {
                    int newPin = readInt("Enter your new 4-digit PIN (1000 - 9999): ", 1000, 9999);
                    admins[currentAdmin].pin = newPin;
                    saveAdmins();
                    printf("Your PIN has been updated!\n");
                    break;
                }
                case 8: save(); printf("Data saved.\n"); break;
            }
        }
    }

    printf("Logged out. Goodbye, %s!\n", admins[currentAdmin].name);
    currentAdmin = -1;
}

void cashierPortal(void) {
    const char *cOpts[] = {"New sale (checkout)", "Return an item", "Exchange an item", "View items", "Back to main menu"};
    int cch;
    while ((cch = menu("CASHIER MENU", cOpts, 5)) != 5) {
        switch (cch) {
            case 1: checkout();     break;
            case 2: returnItem();   break;
            case 3: exchangeItem(); break;
            case 4: browse();       break;
        }
    }
}

void customerPortal(void) {
    const char *uOpts[] = {"View items", "Search by name", "Search by item ID", "Buy items (checkout)", "Back to main menu"};
    int uch;
    while ((uch = menu("CUSTOMER MENU", uOpts, 5)) != 5) {
        switch (uch) {
            case 1: browse();       break;
            case 2: searchByName(); break;
            case 3: searchById();   break;
            case 4: checkout();     break;
        }
    }
}

int main(void) {
    printf("========================================\n");
    printf("     WELCOME TO THE CLOTHING STORE\n");
    printf("========================================\n");
    load();
    loadSales();
    loadAdmins();

    const char *mOpts[] = {"Admin (manager login)", "Cashier (sell, return, exchange)", "Customer (browse, search, buy)", "Save and exit"};
    int ch;
    while ((ch = menu("MAIN MENU", mOpts, 4)) != 4) {
        switch (ch) {
            case 1: if (login()) adminPortal(); break;
            case 2: cashierPortal();            break;
            case 3: customerPortal();           break;
        }
    }
    save();        /* auto-save inventory & sales on exit */
    saveAdmins();  /* auto-save admin credentials on exit */
    free(inv);     /* release dynamic memory */
    printf("Data saved. Thank you, goodbye!\n");
    return 0;
}
