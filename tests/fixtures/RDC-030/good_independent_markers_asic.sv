// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile asic
// Each stage carries an enabling marker; an ASYNC_REG = "FALSE" beside sverity_sync on the same stage does not unmark it, and the attribute order does not matter.
// expect-none: RDC-030
module top(input logic clka, clkb, rst_n, d, output logic q);
  (* ASYNC_REG = "FALSE", sverity_sync = 1 *) logic s1;
  (* sverity_sync = 1 *) logic s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clka or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule
