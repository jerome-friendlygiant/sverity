// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clka) v[3:2] <= d[3:2]; => always_ff @(posedge clka or negedge rst_n) if (!rst_n) v[3:2] <= 2'b0; else v[3:2] <= d[3:2];
// revert-expect-none: RDC-020 WARNING
// expect: RDC-020 WARNING
module top(input logic clka, rst_n, input logic [3:0] d, output logic [3:0] q, output logic y);
  logic [3:0] v;
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) v[1:0] <= 2'b0;
    else begin v[1:0] <= d[1:0]; end
  end
  always_ff @(posedge clka) v[3:2] <= d[3:2];
  assign q = v; assign y = v[3];
endmodule
