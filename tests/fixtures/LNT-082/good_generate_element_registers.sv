// sverity-args: --top top
// One word per generate block, each written and read by its own process.
// expect-none: LNT-082
module top(input logic clk, input logic [3:0] d [2], output logic [3:0] q [2]);
  logic [3:0] a [2];
  for (genvar g = 0; g < 2; g++) begin : gen
    always_ff @(posedge clk) begin a[g] = d[g]; q[g] <= a[g]; end
  end
endmodule
