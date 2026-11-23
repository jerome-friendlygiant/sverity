// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-count: CDC-024 WARNING 4
// expect-unresolved: CDC-024
// expect-count: RDC-023 WARNING 4
// expect-unresolved: RDC-023
// expect-none: CDC-034
// Shared full/empty control flow leaves the two possible pointer pairings ambiguous.
module top(input logic clka, clkb, rst_n, winc, rinc);
  logic [3:0] wb1, wg1, rb1, rg1, ws1a, ws1b, rs1a, rs1b;
  logic [3:0] wb2, wg2, rb2, rg2, ws2a, ws2b, rs2a, rs2b;
  logic any_full, any_empty;
  assign any_full = (wg1 == rs1b) | (wg2 == rs2b);
  assign any_empty = (rg1 == ws1b) | (rg2 == ws2b);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wb1 <= '0; else if (winc && !any_full) wb1 <= wb1 + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wb2 <= '0; else if (winc && !any_full) wb2 <= wb2 + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wg1 <= '0; else wg1 <= wb1 ^ (wb1 >> 1);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wg2 <= '0; else wg2 <= wb2 ^ (wb2 >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rb1 <= '0; else if (rinc && !any_empty) rb1 <= rb1 + 1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rb2 <= '0; else if (rinc && !any_empty) rb2 <= rb2 + 1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rg1 <= '0; else rg1 <= rb1 ^ (rb1 >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rg2 <= '0; else rg2 <= rb2 ^ (rb2 >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {ws1b, ws1a} <= '0; else {ws1b, ws1a} <= {ws1a, wg1};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {ws2b, ws2a} <= '0; else {ws2b, ws2a} <= {ws2a, wg2};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {rs1b, rs1a} <= '0; else {rs1b, rs1a} <= {rs1a, rg1};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {rs2b, rs2a} <= '0; else {rs2b, rs2a} <= {rs2a, rg2};
endmodule
