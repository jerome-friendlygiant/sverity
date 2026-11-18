// sverity-args: --top top
// A recursive task that writes only its own local has no unknown write effect on the memories of the module.
// expect-none: LNT-049
module top(input logic clk, input int n, input logic [3:0] d, output logic [3:0] y);
  logic [3:0] m1 [0:3];
  logic [3:0] m2 [0:3];
  task automatic count(input int k);
    int steps;
    steps = k;
    if (k > 0) count(k - 1);
  endtask
  always_ff @(posedge clk) begin
    count(n);
    m1[0] <= d;
  end
  assign y = m1[0] ^ m2[0];
endmodule
