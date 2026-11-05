// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_binary_pointer_no_reset.sv: only the write pointer crosses into clkb; the read-side Gray pointer and its synchroniser are removed, so there is no opposite-direction pair.
// expect-none: CDC-034
module top(input logic clka, clkb, rst_n, winc, output logic [3:0] wq);
  logic [3:0] wb, wg, ws1, ws2;
  always_ff @(posedge clka) if (winc) wb <= wb + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) wg <= '0; else wg <= wb ^ (wb >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {ws2, ws1} <= '0; else {ws2, ws1} <= {ws1, wg};
  assign wq = ws2;
endmodule
