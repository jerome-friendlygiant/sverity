// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_fifo_pointers_same_reset.sv: the write Gray pointer register lost its asynchronous reset.
// expect: CDC-034 WARNING
// revert: always_ff @(posedge clka) wg <= wb ^ (wb >> 1); => always_ff @(posedge clka or negedge rst_n) if (!rst_n) wg <= '0; else wg <= wb ^ (wb >> 1);
// revert-expect-none: CDC-034 WARNING
module top(input logic clka, clkb, rst_n, winc, rinc, output logic [3:0] wq, rq);
  logic [3:0] wb, wg, rb, rg, ws1, ws2, rs1, rs2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wb <= '0; else if (winc) wb <= wb + 1;
  always_ff @(posedge clka) wg <= wb ^ (wb >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rb <= '0; else if (rinc) rb <= rb + 1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rg <= '0; else rg <= rb ^ (rb >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {ws2, ws1} <= '0; else {ws2, ws1} <= {ws1, wg};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {rs2, rs1} <= '0; else {rs2, rs1} <= {rs1, rg};
  assign wq = ws2; assign rq = rs2;
endmodule
