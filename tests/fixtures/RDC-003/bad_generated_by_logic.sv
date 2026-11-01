// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: RDC-003 WARNING
module top(input logic clka, ra, rb, d, output logic q);
  logic rst_n;
  assign rst_n = ra & rb;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
