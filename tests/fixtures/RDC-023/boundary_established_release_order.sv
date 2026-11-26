// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/boundary_established_release_order.json
// Gray FIFO pair whose clkb side is released by a three-stage synchroniser (c3) under rst_a_n. The declared order (rst_a_n before c3,
// N=2) is established by the RTL and cited on RDC-023 as evidence; the release order of the two domains stays WARNING.
// expect: RDC-023 WARNING
// expect-count: RDC-023 WARNING 1
// expect: CST-016 INFO
// expect-none: CST-016 WARNING
module top(input logic clka, clkb, rst_a_n, winc, rinc, output logic [3:0] wq, rq);
  logic [3:0] wb, wg, rb, rg, ws1, ws2, rs1, rs2;
  logic c1, c2, c3;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) c1 <= 1'b0; else c1 <= 1'b1;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) c2 <= 1'b0; else c2 <= c1;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) c3 <= 1'b0; else c3 <= c2;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) wb <= '0; else if (winc && (wb != rs2)) wb <= wb + 1;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) wg <= '0; else wg <= wb ^ (wb >> 1);
  always_ff @(posedge clkb or negedge c3) if (!c3) rb <= '0; else if (rinc && (rb != ws2)) rb <= rb + 1;
  always_ff @(posedge clkb or negedge c3) if (!c3) rg <= '0; else rg <= rb ^ (rb >> 1);
  always_ff @(posedge clkb or negedge c3) if (!c3) {ws2, ws1} <= '0; else {ws2, ws1} <= {ws1, wg};
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) {rs2, rs1} <= '0; else {rs2, rs1} <= {rs1, rg};
  assign wq = ws2; assign rq = rs2;
endmodule
