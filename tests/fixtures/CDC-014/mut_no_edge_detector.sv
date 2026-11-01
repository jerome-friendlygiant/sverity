// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_toggle_sync.sv: the destination edge detector is removed.
// expect: CDC-014 WARNING
module top(input logic clka, clkb, rst_n, pulse, output logic ev);
  logic tgl, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) tgl <= 1'b0; else if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, tgl};
  assign ev = s2;
endmodule
