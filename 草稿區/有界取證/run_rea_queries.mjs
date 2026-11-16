// Backward-compatible two-argument REA entry. Saved responses and readiness differ.
import { runReaQueries } from './engine_research/runner.mjs';
const [planPath,output]=process.argv.slice(2);
const result=await runReaQueries({planPath,output});
console.log(JSON.stringify(result));
if(result.status!=='COMPLETE')process.exitCode=1;
