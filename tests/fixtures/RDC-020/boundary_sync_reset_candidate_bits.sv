// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Bits v[3:2] have no asynchronous reset but a synchronous reset candidate (if (!sync_rst) ...), so no bit of v is unreset and the symbol is exempt.
// expect-none: RDC-020
module top(input logic clka, rst_n, sync_rst, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] v;
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) v[1:0] <= 2'b0;
    else v[1:0] <= d[1:0];
  end
  always_ff @(posedge clka) begin
    if (!sync_rst) v[3:2] <= 2'b0;
    else v[3:2] <= d[3:2];
  end
  assign q = v;
endmodule
