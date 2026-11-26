// sverity-args: --top top
// set_n is tested inside the branch of rst_n, so the clear of q happens only under a nested condition: whether rst_n forces q is
// not established, and the conflict with the set is Unresolved (RDC-026), not Established.
// expect-unresolved: RDC-026
module top(input logic clka, rst_n, set_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n or negedge set_n) begin
    if (!rst_n) begin if (!set_n) q <= 0; end
    else if (!set_n) q <= 1;
    else q <= d;
  end
endmodule
