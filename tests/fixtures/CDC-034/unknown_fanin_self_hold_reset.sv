// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-034 WARNING
// expect-unresolved: CDC-034
// expect-none: CDC-034 INFO
// expect-coverage: reset-extraction INCOMPLETE
// Binary pointer fan-in holds its value in the asynchronous reset branch.
module top(input logic clka, clkb, rst_n, winc, rinc, output logic [3:0] wq, rq);
  logic [3:0] wb, wg, rb, rg, ws1, ws2, rs1, rs2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wb <= wb; else if (winc && (wb != rs2)) wb <= wb + 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wg <= '0; else wg <= wb ^ (wb >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rb <= '0; else if (rinc && (rb != ws2)) rb <= rb + 1'b1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rg <= '0; else rg <= rb ^ (rb >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {ws2, ws1} <= '0; else {ws2, ws1} <= {ws1, wg};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {rs2, rs1} <= '0; else {rs2, rs1} <= {rs1, rg};
  assign wq = ws2; assign rq = rs2;
endmodule
