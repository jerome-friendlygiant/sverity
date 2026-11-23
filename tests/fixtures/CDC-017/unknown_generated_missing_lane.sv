// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-unresolved: CDC-009
// expect-none: CDC-018
// expect-coverage: cdc-017 INCOMPLETE
// Incomplete generated capture lane set: 4-bit Gray pointer with only 3 generated lanes.
module top(input logic clka, clkb, rst_n, output wire [2:0] q);
  logic [3:0] bin, gray;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else bin <= bin + 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  for (genvar i = 0; i < 3; i++) begin : lanes
    logic first, second;
    always_ff @(posedge clkb or negedge rst_n)
      if (!rst_n) {second, first} <= '0;
      else {second, first} <= {first, gray[i]};
    assign q[i] = second;
  end
endmodule
