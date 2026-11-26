// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: {v[1], v[0]} <= {v[1], 1'b0}; => {v[1], v[0]} <= 2'b00;
// revert-expect-none: RDC-020 WARNING
// expect: RDC-020 WARNING
// v[1] is written from its own value inside the reset branch's concatenation: it holds during reset, so only v[0] is reset.
module top(input logic clka, rst_n, d, output logic q, output wire y);
  logic [1:0] v;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {v[1], v[0]} <= {v[1], 1'b0}; else v <= {d, d};
  assign y = v[1];
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= v[1];
endmodule
