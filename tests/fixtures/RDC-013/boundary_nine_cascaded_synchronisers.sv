// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Nine two-stage synchronisers are cascaded: each asynchronously takes the previous one's output. The asynchronous source is still rst_n alone, so the one-per-source obligation appears once, and no cell output is taken for a second source.
// expect-count: RDC-013 WARNING 1
module top(input logic clka, clkb, rst_n, d, output logic q);
  wire [9:0] root_n;
  assign root_n[0] = rst_n;
  for (genvar i = 0; i < 9; ++i) begin : g
    logic [1:0] sync;
    always_ff @(posedge clka or negedge root_n[i]) if (!root_n[i]) sync <= '0; else sync <= {sync[0], 1'b1};
    assign root_n[i+1] = sync[1];
  end
  always_ff @(posedge clka or negedge root_n[9]) if (!root_n[9]) q <= 1'b0; else q <= d;
endmodule
