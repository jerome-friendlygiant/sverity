// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-unresolved: CDC-034
// expect-unresolved: CDC-024
// expect-unresolved: RDC-023
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-unresolved: CDC-024
// expect-unresolved: RDC-023
// expect-coverage: fifo-coupling INCOMPLETE
// FIFO whose full and empty flags each reach their counters through 300-stage same-domain pipelines.
module top(input logic clka, clkb, rst_n, winc, rinc, output logic full, empty);
  logic [3:0] wb, wg, rb, rg, ws1, ws2, rs1, rs2;
  logic [299:0] fpipe, epipe;
  assign full = (wg == {~rs2[3:2], rs2[1:0]});
  assign empty = (rg == ws2);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) fpipe <= '0; else fpipe <= {fpipe[298:0], full};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) epipe <= '0; else epipe <= {epipe[298:0], empty};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wb <= '0; else if (winc && !fpipe[299]) wb <= wb + 1'b1;
  always_ff @(posedge clka) wg <= wb ^ (wb >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rb <= '0; else if (rinc && !epipe[299]) rb <= rb + 1'b1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) rg <= '0; else rg <= rb ^ (rb >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {ws2, ws1} <= '0; else {ws2, ws1} <= {ws1, wg};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {rs2, rs1} <= '0; else {rs2, rs1} <= {rs1, rg};
endmodule
