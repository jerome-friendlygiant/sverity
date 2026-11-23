// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A second combinational driver from a submodule output port invalidates single-driver staged expansion.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-none: CDC-028 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module raw_drv(input logic [3:0] in_d, output logic [3:0] out_d);
  assign out_d = in_d;
endmodule
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d1, d2, output logic [3:0] q);
  logic [3:0] data, other_data;
  wire [3:0] data_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; other_data <= '0; v_a <= 1'b0; end
    else begin data <= d1; other_data <= d2; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  assign data_d = v2 ? data : q;
  raw_drv u_drv(.in_d(other_data), .out_d(data_d));
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
